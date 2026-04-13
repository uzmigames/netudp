#ifndef NETUDP_SOCKET_URING_H
#define NETUDP_SOCKET_URING_H

/**
 * @file socket_uring.h
 * @brief io_uring socket backend for Linux 5.7+.
 *
 * Provides zero-syscall-overhead I/O by submitting and completing
 * sendmsg/recvmsg operations through shared ring buffers.
 *
 * Compile guard: NETUDP_HAS_IO_URING (set by CMake when liburing is found).
 * When not defined, all functions are stubs returning -1.
 */

#include "socket.h"

namespace netudp {

/** Opaque io_uring context (forward-declared to avoid liburing.h in public headers). */
struct UringContext;

/** io_uring-backed socket with its own submission/completion queues. */
struct UringSocket {
    Socket          base;    /**< Underlying UDP socket (still used for bind). */
    UringContext*   uring = nullptr;
};

/**
 * Create an io_uring socket. Falls back to regular socket if uring init fails.
 * @param ring_size  SQ/CQ ring entries (default 64, must be power of 2).
 * @return NETUDP_OK on success (uring may or may not be active — check uring_is_active).
 */
int uring_socket_create(UringSocket* out, const netudp_address_t* bind_addr,
                         int send_buf_size, int recv_buf_size, int ring_size = 64);

/** Returns true if io_uring is actively being used (not fallback). */
bool uring_is_active(const UringSocket* us);

/**
 * Submit up to max_pkts recvmsg operations and reap completions.
 * @return Number of packets received, 0 if none ready, -1 on error.
 */
int uring_recv_batch(UringSocket* us, SocketPacket* pkts, int max_pkts, int buf_len);

/**
 * Submit sendmsg operations for count packets and flush.
 * @return Number of packets sent, -1 on error.
 */
int uring_send_batch(UringSocket* us, const SocketPacket* pkts, int count);

/** Destroy io_uring context and close the underlying socket. */
void uring_socket_destroy(UringSocket* us);

} // namespace netudp

#endif /* NETUDP_SOCKET_URING_H */
