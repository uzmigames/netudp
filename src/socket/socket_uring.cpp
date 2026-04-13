#include "socket_uring.h"
#include "../profiling/profiler.h"
#include "../core/log.h"

#include <cstring>

#ifdef NETUDP_HAS_IO_URING

#include <liburing.h>
#include <sys/socket.h>
#include <netinet/in.h>

namespace netudp {

/* ======================================================================
 * UringContext — wraps the io_uring instance
 * ====================================================================== */

struct UringContext {
    struct io_uring ring;
    bool            initialized = false;
};

/* ======================================================================
 * Create / Destroy
 * ====================================================================== */

int uring_socket_create(UringSocket* out, const netudp_address_t* bind_addr,
                         int send_buf_size, int recv_buf_size, int ring_size) {
    if (out == nullptr || bind_addr == nullptr) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    /* Create the regular socket first (needed for bind + fallback) */
    int rc = socket_create(&out->base, bind_addr, send_buf_size, recv_buf_size,
                           kSocketFlagReusePort);
    if (rc != NETUDP_OK) {
        return rc;
    }

    /* Initialize io_uring */
    out->uring = new (std::nothrow) UringContext();
    if (out->uring == nullptr) {
        NLOG_WARN("[netudp] uring: allocation failed, using recvmmsg fallback");
        return NETUDP_OK; /* Socket works, just no uring */
    }

    struct io_uring_params params;
    std::memset(&params, 0, sizeof(params));

    int ret = io_uring_queue_init_params(static_cast<unsigned>(ring_size),
                                          &out->uring->ring, &params);
    if (ret < 0) {
        NLOG_WARN("[netudp] uring: init failed (err=%d), using recvmmsg fallback", -ret);
        delete out->uring;
        out->uring = nullptr;
        return NETUDP_OK; /* Socket works, just no uring */
    }

    /* Check for FAST_POLL support (kernel 5.7+) */
    if (!(params.features & IORING_FEAT_FAST_POLL)) {
        NLOG_WARN("[netudp] uring: kernel lacks FAST_POLL, using recvmmsg fallback");
        io_uring_queue_exit(&out->uring->ring);
        delete out->uring;
        out->uring = nullptr;
        return NETUDP_OK;
    }

    /* Register the socket file descriptor for faster submissions */
    int fd = out->base.handle;
    ret = io_uring_register_files(&out->uring->ring, &fd, 1);
    if (ret < 0) {
        NLOG_DEBUG("[netudp] uring: register_files failed (err=%d), continuing without", -ret);
        /* Non-fatal — uring still works, just slightly slower */
    }

    out->uring->initialized = true;
    NLOG_INFO("[netudp] uring: initialized (ring_size=%d, features=0x%x)",
              ring_size, params.features);
    return NETUDP_OK;
}

bool uring_is_active(const UringSocket* us) {
    return (us != nullptr && us->uring != nullptr && us->uring->initialized);
}

void uring_socket_destroy(UringSocket* us) {
    if (us == nullptr) { return; }
    if (us->uring != nullptr) {
        if (us->uring->initialized) {
            io_uring_queue_exit(&us->uring->ring);
        }
        delete us->uring;
        us->uring = nullptr;
    }
    socket_destroy(&us->base);
}

/* ======================================================================
 * Receive — submit recvmsg SQEs, then reap CQEs
 * ====================================================================== */

int uring_recv_batch(UringSocket* us, SocketPacket* pkts, int max_pkts, int buf_len) {
    NETUDP_ZONE("uring::recv_batch");

    if (!uring_is_active(us)) {
        return socket_recv_batch(&us->base, pkts, max_pkts, buf_len);
    }

    struct io_uring* ring = &us->uring->ring;
    const int count = (max_pkts > kSocketBatchMax) ? kSocketBatchMax : max_pkts;

    /* Stack-allocate per-packet structures */
    struct msghdr    msgs[kSocketBatchMax];
    struct iovec     iovs[kSocketBatchMax];
    struct sockaddr_storage addrs[kSocketBatchMax];

    /* Submit recvmsg SQEs */
    int submitted = 0;
    for (int i = 0; i < count; ++i) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
        if (sqe == nullptr) { break; }

        std::memset(&msgs[i], 0, sizeof(struct msghdr));
        std::memset(&addrs[i], 0, sizeof(struct sockaddr_storage));

        iovs[i].iov_base = pkts[i].data;
        iovs[i].iov_len  = static_cast<size_t>(buf_len);

        msgs[i].msg_name    = &addrs[i];
        msgs[i].msg_namelen = sizeof(struct sockaddr_storage);
        msgs[i].msg_iov     = &iovs[i];
        msgs[i].msg_iovlen  = 1;

        io_uring_prep_recvmsg(sqe, us->base.handle, &msgs[i], 0);
        sqe->user_data = static_cast<uint64_t>(i);
        ++submitted;
    }

    if (submitted == 0) { return 0; }

    /* Submit all SQEs to the kernel */
    int ret = io_uring_submit(ring);
    if (ret < 0) { return -1; }

    /* Reap completions (non-blocking peek) */
    int received = 0;
    struct io_uring_cqe* cqe = nullptr;

    for (int i = 0; i < submitted; ++i) {
        ret = io_uring_peek_cqe(ring, &cqe);
        if (ret < 0 || cqe == nullptr) { break; }

        if (cqe->res > 0) {
            auto idx = static_cast<int>(cqe->user_data);
            pkts[idx].len = cqe->res;

            /* Convert sockaddr to netudp_address_t */
            if (addrs[idx].ss_family == AF_INET6) {
                const auto* sin6 = reinterpret_cast<const struct sockaddr_in6*>(&addrs[idx]);
                pkts[idx].addr.type = NETUDP_ADDRESS_IPV6;
                pkts[idx].addr.port = ntohs(sin6->sin6_port);
                for (int j = 0; j < 8; ++j) {
                    auto jj = static_cast<size_t>(j);
                    pkts[idx].addr.data.ipv6[j] = static_cast<uint16_t>(
                        (static_cast<uint16_t>(sin6->sin6_addr.s6_addr[(jj * 2U)])     << 8) |
                         static_cast<uint16_t>(sin6->sin6_addr.s6_addr[(jj * 2U) + 1U]));
                }
            } else {
                const auto* sin4 = reinterpret_cast<const struct sockaddr_in*>(&addrs[idx]);
                pkts[idx].addr.type = NETUDP_ADDRESS_IPV4;
                pkts[idx].addr.port = ntohs(sin4->sin_port);
                std::memcpy(pkts[idx].addr.data.ipv4, &sin4->sin_addr, 4);
            }
            ++received;
        }

        io_uring_cqe_seen(ring, cqe);
    }

    return received;
}

/* ======================================================================
 * Send — submit sendmsg SQEs, then flush
 * ====================================================================== */

int uring_send_batch(UringSocket* us, const SocketPacket* pkts, int count) {
    NETUDP_ZONE("uring::send_batch");

    if (!uring_is_active(us)) {
        return socket_send_batch(&us->base, pkts, count);
    }

    struct io_uring* ring = &us->uring->ring;
    const int batch = (count > kSocketBatchMax) ? kSocketBatchMax : count;

    struct msghdr    msgs[kSocketBatchMax];
    struct iovec     iovs[kSocketBatchMax];
    struct sockaddr_storage addrs[kSocketBatchMax];

    int submitted = 0;
    for (int i = 0; i < batch; ++i) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
        if (sqe == nullptr) { break; }

        std::memset(&msgs[i], 0, sizeof(struct msghdr));
        std::memset(&addrs[i], 0, sizeof(struct sockaddr_storage));

        /* Convert netudp address to sockaddr */
        int addr_len = 0;
        if (pkts[i].addr.type == NETUDP_ADDRESS_IPV6) {
            auto* sin6 = reinterpret_cast<struct sockaddr_in6*>(&addrs[i]);
            sin6->sin6_family = AF_INET6;
            sin6->sin6_port = htons(pkts[i].addr.port);
            for (int j = 0; j < 8; ++j) {
                auto jj = static_cast<size_t>(j);
                sin6->sin6_addr.s6_addr[(jj * 2U)]     = static_cast<uint8_t>(pkts[i].addr.data.ipv6[j] >> 8);
                sin6->sin6_addr.s6_addr[(jj * 2U) + 1U] = static_cast<uint8_t>(pkts[i].addr.data.ipv6[j] & 0xFF);
            }
            addr_len = sizeof(struct sockaddr_in6);
        } else {
            auto* sin4 = reinterpret_cast<struct sockaddr_in*>(&addrs[i]);
            sin4->sin_family = AF_INET;
            sin4->sin_port = htons(pkts[i].addr.port);
            std::memcpy(&sin4->sin_addr, pkts[i].addr.data.ipv4, 4);
            addr_len = sizeof(struct sockaddr_in);
        }

        iovs[i].iov_base = pkts[i].data;
        iovs[i].iov_len  = static_cast<size_t>(pkts[i].len);

        msgs[i].msg_name    = &addrs[i];
        msgs[i].msg_namelen = static_cast<socklen_t>(addr_len);
        msgs[i].msg_iov     = &iovs[i];
        msgs[i].msg_iovlen  = 1;

        io_uring_prep_sendmsg(sqe, us->base.handle, &msgs[i], 0);
        sqe->user_data = static_cast<uint64_t>(i);
        ++submitted;
    }

    if (submitted == 0) { return 0; }

    /* Submit and wait for all completions */
    int ret = io_uring_submit_and_wait(ring, static_cast<unsigned>(submitted));
    if (ret < 0) { return -1; }

    /* Reap completions */
    int sent = 0;
    struct io_uring_cqe* cqe = nullptr;
    for (int i = 0; i < submitted; ++i) {
        ret = io_uring_peek_cqe(ring, &cqe);
        if (ret < 0 || cqe == nullptr) { break; }
        if (cqe->res >= 0) { ++sent; }
        io_uring_cqe_seen(ring, cqe);
    }

    return sent;
}

} // namespace netudp

#else /* !NETUDP_HAS_IO_URING — fallback to standard socket API */

/* ======================================================================
 * Fallback: delegates directly to the standard socket_* functions.
 * io_uring is not available — all operations go through recvmmsg/sendmmsg
 * (Linux) or recvfrom/sendto loops (Windows/macOS).
 * ====================================================================== */

namespace netudp {

int uring_socket_create(UringSocket* out, const netudp_address_t* bind_addr,
                         int send_buf_size, int recv_buf_size, int ring_size) {
    (void)ring_size;
    if (out == nullptr || bind_addr == nullptr) {
        return NETUDP_ERROR_INVALID_PARAM;
    }
    out->uring = nullptr;
    return socket_create(&out->base, bind_addr, send_buf_size, recv_buf_size);
}

bool uring_is_active(const UringSocket*) { return false; }

int uring_recv_batch(UringSocket* us, SocketPacket* pkts, int max_pkts, int buf_len) {
    if (us == nullptr) { return -1; }
    return socket_recv_batch(&us->base, pkts, max_pkts, buf_len);
}

int uring_send_batch(UringSocket* us, const SocketPacket* pkts, int count) {
    if (us == nullptr) { return -1; }
    return socket_send_batch(&us->base, pkts, count);
}

void uring_socket_destroy(UringSocket* us) {
    if (us == nullptr) { return; }
    socket_destroy(&us->base);
    us->uring = nullptr;
}

} // namespace netudp

#endif /* NETUDP_HAS_IO_URING */
