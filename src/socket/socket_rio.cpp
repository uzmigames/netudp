#include "socket_rio.h"
#include "../profiling/profiler.h"
#include "../core/log.h"

#include <netudp/netudp_config.h>
#include <cstring>

/* ======================================================================
 * RIO implementation (Windows 8+ with NETUDP_HAS_RIO)
 * ====================================================================== */

#if defined(NETUDP_HAS_RIO) && defined(NETUDP_PLATFORM_WINDOWS)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>

namespace netudp {

/* ── Constants ───────────────────────────────────────────────────────── */

static constexpr int kRioMaxSlots = 64;  /* Matches kSocketBatchMax */
static constexpr int kRioRecvBufSize = NETUDP_MAX_PACKET_ON_WIRE;

/* ── RIO Context ─────────────────────────────────────────────────────── */

struct RioContext {
    RIO_EXTENSION_FUNCTION_TABLE fn;  /* RIO function table from WSAIoctl */
    RIO_CQ   cq       = RIO_INVALID_CQ;
    RIO_RQ   rq       = RIO_INVALID_RQ;

    /* Pre-registered recv buffer pool */
    char*         recv_pool     = nullptr;
    RIO_BUFFERID  recv_buf_id   = RIO_INVALID_BUFFERID;
    int           recv_depth    = 0;
    int           recv_posted   = 0;  /* How many RIOReceive ops are outstanding */

    /* Pre-registered send buffer pool */
    char*         send_pool     = nullptr;
    RIO_BUFFERID  send_buf_id   = RIO_INVALID_BUFFERID;
    int           send_depth    = 0;

    /* Pre-registered address buffer for recvfrom source addresses */
    SOCKADDR_INET*  addr_pool   = nullptr;
    RIO_BUFFERID    addr_buf_id = RIO_INVALID_BUFFERID;

    bool initialized = false;
};

/* ── Helpers ─────────────────────────────────────────────────────────── */

static bool load_rio_functions(SOCKET sock, RIO_EXTENSION_FUNCTION_TABLE* table) {
    GUID rio_guid = WSAID_MULTIPLE_RIO;
    DWORD bytes = 0;
    int rc = WSAIoctl(sock, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
                      &rio_guid, sizeof(rio_guid),
                      table, sizeof(*table), &bytes, nullptr, nullptr);
    return (rc == 0);
}

static void sockaddr_inet_to_address(const SOCKADDR_INET* sa, netudp_address_t* addr) {
    std::memset(addr, 0, sizeof(*addr));
    if (sa->si_family == AF_INET6) {
        addr->type = NETUDP_ADDRESS_IPV6;
        addr->port = ntohs(sa->Ipv6.sin6_port);
        for (int i = 0; i < 8; ++i) {
            auto idx = static_cast<size_t>(i);
            addr->data.ipv6[i] = static_cast<uint16_t>(
                (static_cast<uint16_t>(sa->Ipv6.sin6_addr.u.Byte[(idx * 2U)])     << 8) |
                 static_cast<uint16_t>(sa->Ipv6.sin6_addr.u.Byte[(idx * 2U) + 1U]));
        }
    } else {
        addr->type = NETUDP_ADDRESS_IPV4;
        addr->port = ntohs(sa->Ipv4.sin_port);
        std::memcpy(addr->data.ipv4, &sa->Ipv4.sin_addr, 4);
    }
}

static void address_to_sockaddr_inet(const netudp_address_t* addr, SOCKADDR_INET* sa) {
    std::memset(sa, 0, sizeof(*sa));
    if (addr->type == NETUDP_ADDRESS_IPV6) {
        sa->Ipv6.sin6_family = AF_INET6;
        sa->Ipv6.sin6_port = htons(addr->port);
        for (int i = 0; i < 8; ++i) {
            auto idx = static_cast<size_t>(i);
            sa->Ipv6.sin6_addr.u.Byte[(idx * 2U)]     = static_cast<UCHAR>(addr->data.ipv6[i] >> 8);
            sa->Ipv6.sin6_addr.u.Byte[(idx * 2U) + 1U] = static_cast<UCHAR>(addr->data.ipv6[i] & 0xFF);
        }
    } else {
        sa->Ipv4.sin_family = AF_INET;
        sa->Ipv4.sin_port = htons(addr->port);
        std::memcpy(&sa->Ipv4.sin_addr, addr->data.ipv4, 4);
    }
}

/* ── Create ──────────────────────────────────────────────────────────── */

int rio_socket_create(RioSocket* out, const netudp_address_t* bind_addr,
                       int send_buf_size, int recv_buf_size, int queue_depth) {
    if (out == nullptr || bind_addr == nullptr) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    /* Create the regular socket first (needed for bind) */
    int rc = socket_create(&out->base, bind_addr, send_buf_size, recv_buf_size);
    if (rc != NETUDP_OK) {
        return rc;
    }

    /* Clamp queue depth */
    if (queue_depth <= 0) { queue_depth = kRioMaxSlots; }
    if (queue_depth > kRioMaxSlots) { queue_depth = kRioMaxSlots; }

    /* Allocate RIO context */
    out->rio = new (std::nothrow) RioContext();
    if (out->rio == nullptr) {
        NLOG_WARN("[netudp] rio: context allocation failed, using WSASendTo fallback");
        return NETUDP_OK;
    }

    std::memset(&out->rio->fn, 0, sizeof(out->rio->fn));

    /* Load RIO function table */
    if (!load_rio_functions(out->base.handle, &out->rio->fn)) {
        NLOG_WARN("[netudp] rio: failed to load function table (pre-Win8?), using WSASendTo fallback");
        delete out->rio;
        out->rio = nullptr;
        return NETUDP_OK;
    }

    auto& fn = out->rio->fn;
    int depth = queue_depth;

    /* Create polled completion queue (no event, no IOCP — lowest latency) */
    out->rio->cq = fn.RIOCreateCompletionQueue(static_cast<DWORD>(depth * 2), nullptr);
    if (out->rio->cq == RIO_INVALID_CQ) {
        NLOG_WARN("[netudp] rio: failed to create CQ, using WSASendTo fallback");
        delete out->rio;
        out->rio = nullptr;
        return NETUDP_OK;
    }

    /* Create request queue */
    out->rio->rq = fn.RIOCreateRequestQueue(
        out->base.handle,
        static_cast<ULONG>(depth),  /* max outstanding recv */
        1,                           /* max recv data buffers per op */
        static_cast<ULONG>(depth),  /* max outstanding send */
        1,                           /* max send data buffers per op */
        out->rio->cq,               /* recv CQ */
        out->rio->cq,               /* send CQ (shared) */
        nullptr                      /* socket context */
    );
    if (out->rio->rq == RIO_INVALID_RQ) {
        NLOG_WARN("[netudp] rio: failed to create RQ (err=%d), using WSASendTo fallback",
                  WSAGetLastError());
        fn.RIOCloseCompletionQueue(out->rio->cq);
        delete out->rio;
        out->rio = nullptr;
        return NETUDP_OK;
    }

    /* Allocate and register recv buffer pool */
    size_t recv_pool_size = static_cast<size_t>(depth) * kRioRecvBufSize;
    out->rio->recv_pool = static_cast<char*>(VirtualAlloc(
        nullptr, recv_pool_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (out->rio->recv_pool == nullptr) {
        NLOG_WARN("[netudp] rio: recv pool alloc failed");
        fn.RIOCloseCompletionQueue(out->rio->cq);
        delete out->rio;
        out->rio = nullptr;
        return NETUDP_OK;
    }
    out->rio->recv_buf_id = fn.RIORegisterBuffer(out->rio->recv_pool,
                                                   static_cast<DWORD>(recv_pool_size));
    if (out->rio->recv_buf_id == RIO_INVALID_BUFFERID) {
        NLOG_WARN("[netudp] rio: recv buffer registration failed");
        VirtualFree(out->rio->recv_pool, 0, MEM_RELEASE);
        fn.RIOCloseCompletionQueue(out->rio->cq);
        delete out->rio;
        out->rio = nullptr;
        return NETUDP_OK;
    }

    /* Allocate and register send buffer pool */
    size_t send_pool_size = static_cast<size_t>(depth) * kRioRecvBufSize;
    out->rio->send_pool = static_cast<char*>(VirtualAlloc(
        nullptr, send_pool_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (out->rio->send_pool == nullptr) {
        NLOG_WARN("[netudp] rio: send pool alloc failed");
        fn.RIODeregisterBuffer(out->rio->recv_buf_id);
        VirtualFree(out->rio->recv_pool, 0, MEM_RELEASE);
        fn.RIOCloseCompletionQueue(out->rio->cq);
        delete out->rio;
        out->rio = nullptr;
        return NETUDP_OK;
    }
    out->rio->send_buf_id = fn.RIORegisterBuffer(out->rio->send_pool,
                                                   static_cast<DWORD>(send_pool_size));
    if (out->rio->send_buf_id == RIO_INVALID_BUFFERID) {
        NLOG_WARN("[netudp] rio: send buffer registration failed");
        VirtualFree(out->rio->send_pool, 0, MEM_RELEASE);
        fn.RIODeregisterBuffer(out->rio->recv_buf_id);
        VirtualFree(out->rio->recv_pool, 0, MEM_RELEASE);
        fn.RIOCloseCompletionQueue(out->rio->cq);
        delete out->rio;
        out->rio = nullptr;
        return NETUDP_OK;
    }

    /* Allocate and register address buffer (for recvfrom source addresses) */
    size_t addr_pool_size = static_cast<size_t>(depth) * sizeof(SOCKADDR_INET);
    out->rio->addr_pool = static_cast<SOCKADDR_INET*>(VirtualAlloc(
        nullptr, addr_pool_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (out->rio->addr_pool == nullptr) {
        NLOG_WARN("[netudp] rio: addr pool alloc failed");
        fn.RIODeregisterBuffer(out->rio->send_buf_id);
        VirtualFree(out->rio->send_pool, 0, MEM_RELEASE);
        fn.RIODeregisterBuffer(out->rio->recv_buf_id);
        VirtualFree(out->rio->recv_pool, 0, MEM_RELEASE);
        fn.RIOCloseCompletionQueue(out->rio->cq);
        delete out->rio;
        out->rio = nullptr;
        return NETUDP_OK;
    }
    out->rio->addr_buf_id = fn.RIORegisterBuffer(
        reinterpret_cast<char*>(out->rio->addr_pool),
        static_cast<DWORD>(addr_pool_size));
    if (out->rio->addr_buf_id == RIO_INVALID_BUFFERID) {
        NLOG_WARN("[netudp] rio: addr buffer registration failed");
        VirtualFree(out->rio->addr_pool, 0, MEM_RELEASE);
        fn.RIODeregisterBuffer(out->rio->send_buf_id);
        VirtualFree(out->rio->send_pool, 0, MEM_RELEASE);
        fn.RIODeregisterBuffer(out->rio->recv_buf_id);
        VirtualFree(out->rio->recv_pool, 0, MEM_RELEASE);
        fn.RIOCloseCompletionQueue(out->rio->cq);
        delete out->rio;
        out->rio = nullptr;
        return NETUDP_OK;
    }

    out->rio->recv_depth = depth;
    out->rio->send_depth = depth;

    /* Pre-post receive operations */
    for (int i = 0; i < depth; ++i) {
        RIO_BUF data_buf;
        data_buf.BufferId = out->rio->recv_buf_id;
        data_buf.Offset   = static_cast<ULONG>(i) * kRioRecvBufSize;
        data_buf.Length    = kRioRecvBufSize;

        RIO_BUF addr_buf;
        addr_buf.BufferId = out->rio->addr_buf_id;
        addr_buf.Offset   = static_cast<ULONG>(i) * static_cast<ULONG>(sizeof(SOCKADDR_INET));
        addr_buf.Length    = static_cast<ULONG>(sizeof(SOCKADDR_INET));

        BOOL ok = fn.RIOReceiveEx(out->rio->rq, &data_buf, 1, nullptr,
                                   &addr_buf, nullptr, nullptr, 0,
                                   reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        if (!ok) {
            NLOG_WARN("[netudp] rio: pre-post recv %d failed (err=%d)", i, WSAGetLastError());
            break;
        }
        out->rio->recv_posted++;
    }

    out->rio->initialized = true;
    NLOG_INFO("[netudp] rio: initialized (depth=%d, recv_posted=%d)",
              depth, out->rio->recv_posted);
    return NETUDP_OK;
}

bool rio_is_active(const RioSocket* rs) {
    return (rs != nullptr && rs->rio != nullptr && rs->rio->initialized);
}

/* ── Destroy ─────────────────────────────────────────────────────────── */

void rio_socket_destroy(RioSocket* rs) {
    if (rs == nullptr) { return; }
    if (rs->rio != nullptr) {
        auto& fn = rs->rio->fn;
        if (rs->rio->initialized) {
            /* Deregister buffers before freeing memory */
            if (rs->rio->addr_buf_id != RIO_INVALID_BUFFERID) {
                fn.RIODeregisterBuffer(rs->rio->addr_buf_id);
            }
            if (rs->rio->send_buf_id != RIO_INVALID_BUFFERID) {
                fn.RIODeregisterBuffer(rs->rio->send_buf_id);
            }
            if (rs->rio->recv_buf_id != RIO_INVALID_BUFFERID) {
                fn.RIODeregisterBuffer(rs->rio->recv_buf_id);
            }
            if (rs->rio->cq != RIO_INVALID_CQ) {
                fn.RIOCloseCompletionQueue(rs->rio->cq);
            }
        }
        if (rs->rio->addr_pool != nullptr) {
            VirtualFree(rs->rio->addr_pool, 0, MEM_RELEASE);
        }
        if (rs->rio->send_pool != nullptr) {
            VirtualFree(rs->rio->send_pool, 0, MEM_RELEASE);
        }
        if (rs->rio->recv_pool != nullptr) {
            VirtualFree(rs->rio->recv_pool, 0, MEM_RELEASE);
        }
        delete rs->rio;
        rs->rio = nullptr;
    }
    socket_destroy(&rs->base);
}

/* ── Receive ─────────────────────────────────────────────────────────── */

int rio_recv_batch(RioSocket* rs, SocketPacket* pkts, int max_pkts, int buf_len) {
    NETUDP_ZONE("rio::recv_batch");
    (void)buf_len;

    if (!rio_is_active(rs)) {
        return socket_recv_batch(&rs->base, pkts, max_pkts, buf_len);
    }

    auto& fn  = rs->rio->fn;
    auto& rio = *rs->rio;
    const int count = (max_pkts > kRioMaxSlots) ? kRioMaxSlots : max_pkts;

    /* Dequeue completions from polled CQ (zero syscall) */
    RIORESULT results[kRioMaxSlots];
    ULONG n = fn.RIODequeueCompletion(rio.cq, results,
                                       static_cast<ULONG>(count));
    if (n == 0 || n == RIO_CORRUPT_CQ) {
        return (n == RIO_CORRUPT_CQ) ? -1 : 0;
    }

    int received = 0;
    for (ULONG i = 0; i < n; ++i) {
        auto slot = static_cast<int>(results[i].RequestContext);
        int bytes = static_cast<int>(results[i].BytesTransferred);

        if (bytes <= 0 || slot < 0 || slot >= rio.recv_depth) {
            continue;
        }

        /* Copy data from registered buffer to caller's packet */
        char* src = rio.recv_pool + (static_cast<size_t>(slot) * kRioRecvBufSize);
        std::memcpy(pkts[received].data, src, static_cast<size_t>(bytes));
        pkts[received].len = bytes;

        /* Convert source address */
        sockaddr_inet_to_address(&rio.addr_pool[slot], &pkts[received].addr);
        ++received;

        /* Re-post this recv slot */
        RIO_BUF data_buf;
        data_buf.BufferId = rio.recv_buf_id;
        data_buf.Offset   = static_cast<ULONG>(slot) * kRioRecvBufSize;
        data_buf.Length    = kRioRecvBufSize;

        RIO_BUF addr_buf;
        addr_buf.BufferId = rio.addr_buf_id;
        addr_buf.Offset   = static_cast<ULONG>(slot) * static_cast<ULONG>(sizeof(SOCKADDR_INET));
        addr_buf.Length    = static_cast<ULONG>(sizeof(SOCKADDR_INET));

        fn.RIOReceiveEx(rio.rq, &data_buf, 1, nullptr,
                         &addr_buf, nullptr, nullptr, 0,
                         reinterpret_cast<void*>(static_cast<uintptr_t>(slot)));
    }

    return received;
}

/* ── Send ────────────────────────────────────────────────────────────── */

int rio_send_batch(RioSocket* rs, const SocketPacket* pkts, int count) {
    NETUDP_ZONE("rio::send_batch");

    if (!rio_is_active(rs)) {
        return socket_send_batch(&rs->base, pkts, count);
    }

    auto& fn  = rs->rio->fn;
    auto& rio = *rs->rio;
    const int batch = (count > kRioMaxSlots) ? kRioMaxSlots : count;

    /* Copy packet data into registered send buffer and submit */
    int submitted = 0;
    for (int i = 0; i < batch; ++i) {
        /* Copy payload into registered send pool */
        char* dst = rio.send_pool + (static_cast<size_t>(i) * kRioRecvBufSize);
        std::memcpy(dst, pkts[i].data, static_cast<size_t>(pkts[i].len));

        /* Convert destination address into addr pool slot */
        address_to_sockaddr_inet(&pkts[i].addr, &rio.addr_pool[i]);

        RIO_BUF data_buf;
        data_buf.BufferId = rio.send_buf_id;
        data_buf.Offset   = static_cast<ULONG>(i) * kRioRecvBufSize;
        data_buf.Length    = static_cast<ULONG>(pkts[i].len);

        RIO_BUF addr_buf;
        addr_buf.BufferId = rio.addr_buf_id;
        addr_buf.Offset   = static_cast<ULONG>(i) * static_cast<ULONG>(sizeof(SOCKADDR_INET));
        addr_buf.Length    = static_cast<ULONG>(sizeof(SOCKADDR_INET));

        BOOL ok = fn.RIOSendEx(rio.rq, &data_buf, 1, nullptr,
                                &addr_buf, nullptr, nullptr, 0, nullptr);
        if (!ok) {
            break;
        }
        ++submitted;
    }

    if (submitted == 0) { return 0; }

    /* Notify kernel to process the submission queue */
    fn.RIONotify(rio.cq);

    /* Dequeue send completions (non-blocking) to free CQ slots */
    RIORESULT send_results[kRioMaxSlots];
    ULONG completed = fn.RIODequeueCompletion(rio.cq, send_results,
                                               static_cast<ULONG>(submitted));

    int sent = 0;
    if (completed != RIO_CORRUPT_CQ) {
        for (ULONG i = 0; i < completed; ++i) {
            if (send_results[i].BytesTransferred > 0) {
                ++sent;
            }
        }
    }

    /* If not all completions came back yet, count submitted as sent
     * (they are in-flight, will complete asynchronously) */
    if (sent == 0 && submitted > 0) {
        sent = submitted;
    }

    return sent;
}

} // namespace netudp

#else /* !NETUDP_HAS_RIO || !NETUDP_PLATFORM_WINDOWS — delegate to standard socket API */

namespace netudp {

int rio_socket_create(RioSocket* out, const netudp_address_t* bind_addr,
                       int send_buf_size, int recv_buf_size, int queue_depth) {
    (void)queue_depth;
    if (out == nullptr || bind_addr == nullptr) {
        return NETUDP_ERROR_INVALID_PARAM;
    }
    out->rio = nullptr;
    return socket_create(&out->base, bind_addr, send_buf_size, recv_buf_size);
}

bool rio_is_active(const RioSocket*) { return false; }

int rio_recv_batch(RioSocket* rs, SocketPacket* pkts, int max_pkts, int buf_len) {
    if (rs == nullptr) { return -1; }
    return socket_recv_batch(&rs->base, pkts, max_pkts, buf_len);
}

int rio_send_batch(RioSocket* rs, const SocketPacket* pkts, int count) {
    if (rs == nullptr) { return -1; }
    return socket_send_batch(&rs->base, pkts, count);
}

void rio_socket_destroy(RioSocket* rs) {
    if (rs == nullptr) { return; }
    socket_destroy(&rs->base);
    rs->rio = nullptr;
}

} // namespace netudp

#endif /* NETUDP_HAS_RIO && NETUDP_PLATFORM_WINDOWS */
