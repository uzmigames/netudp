#ifndef NETUDP_SOCKET_RIO_H
#define NETUDP_SOCKET_RIO_H

/**
 * @file socket_rio.h
 * @brief Registered I/O (RIO) socket backend for Windows 8+.
 *
 * Uses pre-registered buffers and shared-memory completion queues to
 * eliminate per-operation kernel transitions. Polled CQ mode for lowest
 * latency (no event/IOCP overhead).
 *
 * Compile guard: NETUDP_HAS_RIO (set by CMake -DNETUDP_ENABLE_RIO=ON).
 * When not defined, all functions delegate to the standard socket API.
 */

#include "socket.h"

namespace netudp {

/** Opaque RIO context (hides Winsock RIO types from public headers). */
struct RioContext;

/** RIO-backed socket with pre-registered buffers and polled completion queue. */
struct RioSocket {
    Socket      base;           /**< Underlying UDP socket (used for bind). */
    RioContext* rio = nullptr;  /**< RIO state, nullptr if RIO unavailable. */
};

/**
 * Create a RIO socket. Falls back to regular socket if RIO init fails.
 * @param queue_depth  Number of pre-posted recv/send slots (default 64).
 * @return NETUDP_OK on success (rio may be nullptr if RIO unavailable — check rio_is_active).
 */
int rio_socket_create(RioSocket* out, const netudp_address_t* bind_addr,
                       int send_buf_size, int recv_buf_size, int queue_depth = 64);

/** Returns true if RIO is actively being used (not fallback). */
bool rio_is_active(const RioSocket* rs);

/**
 * Receive up to max_pkts datagrams via RIO polled CQ.
 * Falls back to WSARecvFrom loop if RIO is not active.
 */
int rio_recv_batch(RioSocket* rs, SocketPacket* pkts, int max_pkts, int buf_len);

/**
 * Send count datagrams via RIO.
 * Falls back to WSASendTo loop if RIO is not active.
 */
int rio_send_batch(RioSocket* rs, const SocketPacket* pkts, int count);

/** Destroy RIO context and close the underlying socket. */
void rio_socket_destroy(RioSocket* rs);

} // namespace netudp

#endif /* NETUDP_SOCKET_RIO_H */
