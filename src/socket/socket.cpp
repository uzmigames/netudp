#include "socket.h"
#include "../profiling/profiler.h"
#include <netudp/netudp_config.h>
#include <cstring>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/udp.h>
#endif

namespace netudp {

/* ===== Platform init/term ===== */

#ifdef NETUDP_PLATFORM_WINDOWS

int socket_platform_init() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return NETUDP_ERROR_SOCKET;
    }
    return NETUDP_OK;
}

void socket_platform_term() {
    WSACleanup();
}

#else /* Unix */

int socket_platform_init() {
    return NETUDP_OK;
}

void socket_platform_term() {}

#endif

/* ===== Helpers ===== */

void address_to_sockaddr(const netudp_address_t* addr,
                          void* ss_void, int* ss_len) {
    auto* ss = static_cast<struct sockaddr_storage*>(ss_void);
    std::memset(ss, 0, sizeof(*ss));

    if (addr->type == NETUDP_ADDRESS_IPV6) {
        auto* sin6 = reinterpret_cast<struct sockaddr_in6*>(ss);
        sin6->sin6_family = AF_INET6;
        sin6->sin6_port = htons(addr->port);
        for (int i = 0; i < 8; ++i) {
            const auto idx = static_cast<size_t>(i);
            sin6->sin6_addr.s6_addr[(idx * 2U)]      = static_cast<uint8_t>(addr->data.ipv6[i] >> 8);
            sin6->sin6_addr.s6_addr[(idx * 2U) + 1U] = static_cast<uint8_t>(addr->data.ipv6[i] & 0xFF);
        }
        *ss_len = sizeof(struct sockaddr_in6);
    } else {
        auto* sin4 = reinterpret_cast<struct sockaddr_in*>(ss);
        sin4->sin_family = AF_INET;
        sin4->sin_port = htons(addr->port);
        std::memcpy(&sin4->sin_addr, addr->data.ipv4, 4);
        *ss_len = sizeof(struct sockaddr_in);
    }
}

static void sockaddr_to_address(const struct sockaddr_storage* ss,
                                 netudp_address_t* addr) {
    *addr = address_zero();

    if (ss->ss_family == AF_INET6) {
        const auto* sin6 = reinterpret_cast<const struct sockaddr_in6*>(ss);
        addr->type = NETUDP_ADDRESS_IPV6;
        addr->port = ntohs(sin6->sin6_port);
        for (int i = 0; i < 8; ++i) {
            const auto idx = static_cast<size_t>(i);
            addr->data.ipv6[i] = static_cast<uint16_t>(
                (static_cast<uint16_t>(sin6->sin6_addr.s6_addr[(idx * 2U)])      << 8) |
                 static_cast<uint16_t>(sin6->sin6_addr.s6_addr[(idx * 2U) + 1U]));
        }
    } else {
        const auto* sin4 = reinterpret_cast<const struct sockaddr_in*>(ss);
        addr->type = NETUDP_ADDRESS_IPV4;
        addr->port = ntohs(sin4->sin_port);
        std::memcpy(addr->data.ipv4, &sin4->sin_addr, 4);
    }
}

/* ===== Socket operations ===== */

int socket_create(Socket* out, const netudp_address_t* bind_addr,
                  int send_buf_size, int recv_buf_size, int flags) {
    if (out == nullptr || bind_addr == nullptr) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    int af = (bind_addr->type == NETUDP_ADDRESS_IPV6) ? AF_INET6 : AF_INET;

    netudp_socket_handle_t sock = socket(af, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == NETUDP_INVALID_SOCKET) {
        return NETUDP_ERROR_SOCKET;
    }

    /* SO_REUSEADDR (server sockets) */
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    /* SO_REUSEPORT (Linux only — enables kernel-level load balancing across threads) */
#ifdef __linux__
    if ((flags & kSocketFlagReusePort) != 0) {
        int reuseport = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEPORT,
                   reinterpret_cast<const char*>(&reuseport), sizeof(reuseport));
    }
#endif

#ifdef NETUDP_PLATFORM_WINDOWS
    /* SIO_UDP_CONNRESET = FALSE — prevents ICMP port-unreachable from killing
     * recvfrom. Used by netcode.io and MsQuic. Zero perf impact. */
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
    BOOL connreset = FALSE;
    DWORD connreset_bytes = 0;
    WSAIoctl(sock, SIO_UDP_CONNRESET,
             &connreset, sizeof(connreset),
             nullptr, 0, &connreset_bytes, nullptr, nullptr);

    /* NOTE: The following options are disabled for loopback benchmarking because
     * they add kernel overhead on localhost without a real NIC to offload to.
     * Enable them for production deployments with real NICs:
     *
     * - UDP_SEND_MSG_SIZE (USO): NIC segments large sends. No benefit on loopback.
     * - SIO_LOOPBACK_FAST_PATH: Actually HURTS on some Windows builds.
     * - IP_DONTFRAGMENT: Adds DF bit processing overhead on loopback.
     * - UDP_RECV_MAX_COALESCED_SIZE (URO): No NIC to coalesce on loopback.
     * - IP_TOS: Router QoS marking, no effect on loopback.
     * - SO_RCVBUF 16MB: Excessive for loopback, causes kernel memory pressure.
     *
     * These are applied conditionally when NOT loopback in production builds.
     * For now, only SIO_UDP_CONNRESET is unconditional (correctness fix).
     */

    (void)flags;
#endif

#if !defined(__linux__) && !defined(NETUDP_PLATFORM_WINDOWS)
    (void)flags; /* macOS: no SO_REUSEPORT, no SIO_UDP_CONNRESET */
#endif

    setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
               reinterpret_cast<const char*>(&send_buf_size), sizeof(send_buf_size));
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&recv_buf_size), sizeof(recv_buf_size));

    /* IPv6 dual-stack */
    if (af == AF_INET6) {
        int v6only = 0;
        setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY,
                   reinterpret_cast<const char*>(&v6only), sizeof(v6only));
    }

    /* Non-blocking */
#ifdef NETUDP_PLATFORM_WINDOWS
    unsigned long nonblock = 1;
    if (ioctlsocket(sock, FIONBIO, &nonblock) != 0) {
        closesocket(sock);
        return NETUDP_ERROR_SOCKET;
    }
#else
    int fl = fcntl(sock, F_GETFL, 0);
    if (fl == -1 || fcntl(sock, F_SETFL, fl | O_NONBLOCK) == -1) {
        close(sock);
        return NETUDP_ERROR_SOCKET;
    }
#endif

    /* Bind */
    struct sockaddr_storage ss;
    int ss_len = 0;
    address_to_sockaddr(bind_addr, &ss, &ss_len);

    if (bind(sock, reinterpret_cast<const struct sockaddr*>(&ss), ss_len) != 0) {
#ifdef NETUDP_PLATFORM_WINDOWS
        closesocket(sock);
#else
        close(sock);
#endif
        return NETUDP_ERROR_SOCKET;
    }

    out->handle = sock;
    return NETUDP_OK;
}

int socket_send(Socket* sock, const netudp_address_t* dest,
                const void* data, int len) {
    NETUDP_ZONE("sock::send");
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET) {
        return -1;
    }

    struct sockaddr_storage ss;
    int ss_len = 0;
    address_to_sockaddr(dest, &ss, &ss_len);

    int result = sendto(sock->handle,
                        static_cast<const char*>(data), len, 0,
                        reinterpret_cast<const struct sockaddr*>(&ss), ss_len);

#ifdef NETUDP_PLATFORM_WINDOWS
    if (result == SOCKET_ERROR) {
        return -1;
    }
#else
    if (result < 0) {
        return -1;
    }
#endif

    return result;
}

int socket_send_raw(Socket* sock, const void* sa, int sa_len,
                    const void* data, int len) {
    NETUDP_ZONE("sock::send_raw");
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET) {
        return -1;
    }

    int result = sendto(sock->handle,
                        static_cast<const char*>(data), len, 0,
                        static_cast<const struct sockaddr*>(sa), sa_len);

#ifdef NETUDP_PLATFORM_WINDOWS
    if (result == SOCKET_ERROR) {
        return -1;
    }
#else
    if (result < 0) {
        return -1;
    }
#endif

    return result;
}

int socket_recv(Socket* sock, netudp_address_t* from,
                void* buf, int buf_len) {
    NETUDP_ZONE("sock::recv");
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET) {
        return -1;
    }

    struct sockaddr_storage ss;
    std::memset(&ss, 0, sizeof(ss));

#ifdef NETUDP_PLATFORM_WINDOWS
    int ss_len = sizeof(ss);
#else
    socklen_t ss_len = sizeof(ss);
#endif

    int result = recvfrom(sock->handle,
                          static_cast<char*>(buf), buf_len, 0,
                          reinterpret_cast<struct sockaddr*>(&ss), &ss_len);

#ifdef NETUDP_PLATFORM_WINDOWS
    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return 0; /* No data (non-blocking) */
        }
        return -1;
    }
#else
    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
#endif

    if (from != nullptr) {
        sockaddr_to_address(&ss, from);
    }

    return result;
}

int socket_connect(Socket* sock, const netudp_address_t* dest) {
    NETUDP_ZONE("sock::connect");
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET || dest == nullptr) {
        return -1;
    }

    struct sockaddr_storage ss;
    int ss_len = 0;
    address_to_sockaddr(dest, &ss, &ss_len);

    int result = connect(sock->handle, reinterpret_cast<const struct sockaddr*>(&ss), ss_len);
#ifdef NETUDP_PLATFORM_WINDOWS
    return (result == SOCKET_ERROR) ? -1 : 0;
#else
    return (result < 0) ? -1 : 0;
#endif
}

int socket_send_connected(Socket* sock, const void* data, int len) {
    NETUDP_ZONE("sock::send_conn");
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET) {
        return -1;
    }

    int result = send(sock->handle, static_cast<const char*>(data), len, 0);

#ifdef NETUDP_PLATFORM_WINDOWS
    return (result == SOCKET_ERROR) ? -1 : result;
#else
    return (result < 0) ? -1 : result;
#endif
}

void socket_set_cpu_affinity(Socket* sock, int cpu_id) {
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET || cpu_id < 0) {
        return;
    }
#ifdef NETUDP_PLATFORM_WINDOWS
    /* SIO_CPU_AFFINITY — binds socket to a specific CPU for RSS distribution.
     * Used by MsQuic for per-processor socket design. */
#ifndef SIO_CPU_AFFINITY
#define SIO_CPU_AFFINITY _WSAIOW(IOC_VENDOR, 21)
#endif
    DWORD cpu = static_cast<DWORD>(cpu_id);
    DWORD bytes = 0;
    WSAIoctl(sock->handle, SIO_CPU_AFFINITY,
             &cpu, sizeof(cpu), nullptr, 0, &bytes, nullptr, nullptr);
#else
    /* Linux: use thread affinity (sched_setaffinity) instead of socket affinity */
    (void)cpu_id;
#endif
}

void socket_destroy(Socket* sock) {
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET) {
        return;
    }

#ifdef NETUDP_PLATFORM_WINDOWS
    closesocket(sock->handle);
#else
    close(sock->handle);
#endif

    sock->handle = NETUDP_INVALID_SOCKET;
}

/* ===== Batch recv/send ===== */

#ifdef __linux__

int socket_recv_batch(Socket* sock, SocketPacket* pkts, int max_pkts, int buf_len) {
    NETUDP_ZONE("sock::recv_batch");
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET ||
        pkts == nullptr || max_pkts <= 0) {
        return -1;
    }

    /* Stack-allocate control structures (max kSocketBatchMax entries). */
    const int count = (max_pkts > kSocketBatchMax) ? kSocketBatchMax : max_pkts;

    struct mmsghdr   mmsg[kSocketBatchMax];
    struct iovec     iov[kSocketBatchMax];
    struct sockaddr_storage addrs[kSocketBatchMax];

    std::memset(mmsg,  0, sizeof(struct mmsghdr)          * static_cast<size_t>(count));
    std::memset(addrs, 0, sizeof(struct sockaddr_storage) * static_cast<size_t>(count));

    for (int i = 0; i < count; ++i) {
        iov[i].iov_base              = pkts[i].data;
        iov[i].iov_len               = static_cast<size_t>(buf_len);
        mmsg[i].msg_hdr.msg_iov      = &iov[i];
        mmsg[i].msg_hdr.msg_iovlen   = 1;
        mmsg[i].msg_hdr.msg_name     = &addrs[i];
        mmsg[i].msg_hdr.msg_namelen  = sizeof(struct sockaddr_storage);
    }

    int received = static_cast<int>(recvmmsg(sock->handle, mmsg,
                                             static_cast<unsigned int>(count),
                                             MSG_DONTWAIT, nullptr));
    if (received <= 0) {
        if (received == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }

    for (int i = 0; i < received; ++i) {
        pkts[i].len = static_cast<int>(mmsg[i].msg_len);
        sockaddr_to_address(&addrs[i], &pkts[i].addr);
    }

    return received;
}

int socket_send_batch(Socket* sock, const SocketPacket* pkts, int count) {
    NETUDP_ZONE("sock::send_batch");
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET ||
        pkts == nullptr || count <= 0) {
        return -1;
    }

    const int batch = (count > kSocketBatchMax) ? kSocketBatchMax : count;

    /* Try GSO: if all packets have the same size AND go to the same destination,
     * concatenate into one super-buffer and let the kernel segment (UDP_SEGMENT).
     * This sends N datagrams in 1 syscall instead of N. */
#ifndef UDP_SEGMENT
#define UDP_SEGMENT 103
#endif
    if (batch > 1) {
        bool same_size = true;
        bool same_dest = true;
        int seg_size = pkts[0].len;
        for (int i = 1; i < batch; ++i) {
            if (pkts[i].len != seg_size) { same_size = false; break; }
            if (std::memcmp(&pkts[i].addr, &pkts[0].addr, sizeof(netudp_address_t)) != 0) { same_dest = false; break; }
        }
        if (same_size && same_dest && seg_size > 0) {
            NETUDP_ZONE("sock::send_gso");
            /* Concatenate all payloads into one contiguous buffer */
            uint8_t super_buf[kSocketBatchMax * NETUDP_MAX_PACKET_ON_WIRE];
            size_t total_len = 0;
            for (int i = 0; i < batch; ++i) {
                std::memcpy(super_buf + total_len, pkts[i].data,
                            static_cast<size_t>(pkts[i].len));
                total_len += static_cast<size_t>(pkts[i].len);
            }

            struct sockaddr_storage dest_addr;
            int dest_len = 0;
            address_to_sockaddr(&pkts[0].addr, &dest_addr, &dest_len);

            struct iovec iov;
            iov.iov_base = super_buf;
            iov.iov_len  = total_len;

            /* Set UDP_SEGMENT via cmsg ancillary data */
            union {
                char buf[CMSG_SPACE(sizeof(uint16_t))];
                struct cmsghdr align;
            } cmsg_buf;

            struct msghdr msg;
            std::memset(&msg, 0, sizeof(msg));
            msg.msg_name       = &dest_addr;
            msg.msg_namelen    = static_cast<socklen_t>(dest_len);
            msg.msg_iov        = &iov;
            msg.msg_iovlen     = 1;
            msg.msg_control    = cmsg_buf.buf;
            msg.msg_controllen = sizeof(cmsg_buf.buf);

            struct cmsghdr* cm = CMSG_FIRSTHDR(&msg);
            cm->cmsg_level = SOL_UDP;
            cm->cmsg_type  = UDP_SEGMENT;
            cm->cmsg_len   = CMSG_LEN(sizeof(uint16_t));
            *reinterpret_cast<uint16_t*>(CMSG_DATA(cm)) = static_cast<uint16_t>(seg_size);

            ssize_t result = sendmsg(sock->handle, &msg, 0);
            if (result >= 0) {
                return batch; /* All segments sent as one syscall */
            }
            /* GSO failed (kernel too old?) — fall through to sendmmsg */
        }
    }

    /* Standard sendmmsg path (fallback or mixed-size/mixed-dest packets) */
    struct mmsghdr  mmsg[kSocketBatchMax];
    struct iovec    iov[kSocketBatchMax];
    struct sockaddr_storage addrs[kSocketBatchMax];

    std::memset(mmsg,  0, sizeof(struct mmsghdr)          * static_cast<size_t>(batch));
    std::memset(addrs, 0, sizeof(struct sockaddr_storage) * static_cast<size_t>(batch));

    for (int i = 0; i < batch; ++i) {
        int ss_len = 0;
        address_to_sockaddr(&pkts[i].addr, &addrs[i], &ss_len);

        iov[i].iov_base              = pkts[i].data;
        iov[i].iov_len               = static_cast<size_t>(pkts[i].len);
        mmsg[i].msg_hdr.msg_iov      = &iov[i];
        mmsg[i].msg_hdr.msg_iovlen   = 1;
        mmsg[i].msg_hdr.msg_name     = &addrs[i];
        mmsg[i].msg_hdr.msg_namelen  = static_cast<socklen_t>(ss_len);
    }

    int sent = static_cast<int>(sendmmsg(sock->handle, mmsg,
                                         static_cast<unsigned int>(batch), 0));
    return (sent < 0) ? -1 : sent;
}

#else /* Windows / macOS — loop fallback */

int socket_recv_batch(Socket* sock, SocketPacket* pkts, int max_pkts, int buf_len) {
    NETUDP_ZONE("sock::recv_batch");
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET ||
        pkts == nullptr || max_pkts <= 0) {
        return -1;
    }

    int received = 0;
    const int limit = (max_pkts > kSocketBatchMax) ? kSocketBatchMax : max_pkts;

#ifdef NETUDP_PLATFORM_WINDOWS
    /* Optimized Windows path: WSARecvFrom avoids sendto compat shim overhead */
    for (int i = 0; i < limit; ++i) {
        WSABUF wsa_buf;
        wsa_buf.buf = static_cast<char*>(pkts[i].data);
        wsa_buf.len = static_cast<ULONG>(buf_len);
        DWORD bytes_recv = 0;
        DWORD wsa_flags = 0;

        struct sockaddr_storage ss;
        std::memset(&ss, 0, sizeof(ss));
        int ss_len = sizeof(ss);

        int result = WSARecvFrom(sock->handle, &wsa_buf, 1, &bytes_recv, &wsa_flags,
                                 reinterpret_cast<struct sockaddr*>(&ss), &ss_len,
                                 nullptr, nullptr);
        if (result == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                break; /* No more data */
            }
            return (received > 0) ? received : -1;
        }
        if (bytes_recv == 0) {
            break;
        }
        pkts[i].len = static_cast<int>(bytes_recv);
        sockaddr_to_address(&ss, &pkts[i].addr);
        ++received;
    }
#else
    /* macOS fallback — loop recvfrom */
    for (int i = 0; i < limit; ++i) {
        int n = socket_recv(sock, &pkts[i].addr, pkts[i].data, buf_len);
        if (n <= 0) {
            break;
        }
        pkts[i].len = n;
        ++received;
    }
#endif

    return received;
}

int socket_send_batch(Socket* sock, const SocketPacket* pkts, int count) {
    NETUDP_ZONE("sock::send_batch");
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET ||
        pkts == nullptr || count <= 0) {
        return -1;
    }

    const int batch = (count > kSocketBatchMax) ? kSocketBatchMax : count;

#ifdef NETUDP_PLATFORM_WINDOWS
    /*
     * Try USO (UDP Segmentation Offload): if all packets have the same size
     * and go to the same destination, concatenate into one large WSASendTo call.
     * The kernel + NIC segment by UDP_SEND_MSG_SIZE into individual datagrams.
     * Windows 10 1703+ with capable NIC. Falls through to WSASendTo loop on failure.
     */
    if (batch > 1) {
        bool same_size = true;
        bool same_dest = true;
        int seg_size = pkts[0].len;
        for (int i = 1; i < batch; ++i) {
            if (pkts[i].len != seg_size) { same_size = false; break; }
            if (std::memcmp(&pkts[i].addr, &pkts[0].addr, sizeof(netudp_address_t)) != 0) { same_dest = false; break; }
        }
        if (same_size && same_dest && seg_size > 0) {
            NETUDP_ZONE("sock::send_uso");
            /* Concatenate payloads into one contiguous buffer */
            uint8_t super_buf[kSocketBatchMax * NETUDP_MAX_PACKET_ON_WIRE];
            ULONG total_len = 0;
            for (int i = 0; i < batch; ++i) {
                std::memcpy(super_buf + total_len, pkts[i].data,
                            static_cast<size_t>(pkts[i].len));
                total_len += static_cast<ULONG>(pkts[i].len);
            }

            struct sockaddr_storage dest_addr;
            int dest_len = 0;
            address_to_sockaddr(&pkts[0].addr, &dest_addr, &dest_len);

            WSABUF wsa_buf;
            wsa_buf.buf = reinterpret_cast<char*>(super_buf);
            wsa_buf.len = total_len;
            DWORD bytes_sent = 0;

            /* Send the super-buffer — kernel segments by UDP_SEND_MSG_SIZE */
            int result = WSASendTo(sock->handle, &wsa_buf, 1, &bytes_sent, 0,
                                    reinterpret_cast<const struct sockaddr*>(&dest_addr),
                                    dest_len, nullptr, nullptr);
            if (result != SOCKET_ERROR) {
                return batch; /* All segments sent in one syscall */
            }
            /* USO failed — fall through to per-packet WSASendTo */
        }
    }

    /* Standard path: pre-convert addresses, WSASendTo in tight loop */
    struct sockaddr_storage addrs[kSocketBatchMax];
    int addr_lens[kSocketBatchMax];

    for (int i = 0; i < batch; ++i) {
        address_to_sockaddr(&pkts[i].addr, &addrs[i], &addr_lens[i]);
    }

    int sent = 0;
    for (int i = 0; i < batch; ++i) {
        WSABUF wsa_buf;
        wsa_buf.buf = static_cast<char*>(pkts[i].data);
        wsa_buf.len = static_cast<ULONG>(pkts[i].len);
        DWORD bytes_sent = 0;

        int result = WSASendTo(sock->handle, &wsa_buf, 1, &bytes_sent, 0,
                               reinterpret_cast<const struct sockaddr*>(&addrs[i]),
                               addr_lens[i], nullptr, nullptr);
        if (result == SOCKET_ERROR) {
            break;
        }
        ++sent;
    }
    return sent;
#else
    /* macOS fallback — loop sendto */
    int sent = 0;
    for (int i = 0; i < batch; ++i) {
        int n = socket_send(sock, &pkts[i].addr,
                            pkts[i].data, pkts[i].len);
        if (n < 0) {
            break;
        }
        ++sent;
    }
    return sent;
#endif
}

#endif /* __linux__ */

} // namespace netudp
