#ifndef NETUDP_SOCKET_H
#define NETUDP_SOCKET_H

/**
 * @file socket.h
 * @brief Platform-independent UDP socket abstraction.
 */

#include "../core/platform.h"
#include "../core/address.h"
#include <netudp/netudp_types.h>

#if defined(NETUDP_PLATFORM_WINDOWS)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET netudp_socket_handle_t;
#define NETUDP_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int netudp_socket_handle_t;
#define NETUDP_INVALID_SOCKET -1
#endif

namespace netudp {

struct Socket {
    netudp_socket_handle_t handle = NETUDP_INVALID_SOCKET;
};

/** Maximum datagrams per batch recv/send call. */
static constexpr int kSocketBatchMax = 64;

/**
 * Single packet entry for batch operations.
 *  recv: addr=source (out), data=pre-allocated buf (in), len=bytes received (out)
 *  send: addr=dest   (in),  data=payload      (in), len=bytes to send   (in)
 */
struct SocketPacket {
    netudp_address_t addr;
    void*            data;
    int              len;
};

/**
 * Initialize platform socket subsystem (WSAStartup on Windows, no-op on Unix).
 * Called once from netudp_init().
 */
int socket_platform_init();

/**
 * Cleanup platform socket subsystem (WSACleanup on Windows, no-op on Unix).
 * Called once from netudp_term().
 */
void socket_platform_term();

/** Socket creation flags. */
static constexpr int kSocketFlagReusePort = 1; /**< SO_REUSEPORT (Linux only) */

/**
 * Create a non-blocking UDP socket, bind to address.
 * Sets SO_SNDBUF/SO_RCVBUF to send_buf_size/recv_buf_size.
 * IPv6 dual-stack: IPV6_V6ONLY=0 when addr is IPv6.
 * @param flags  Bitmask of kSocketFlag* (0 for defaults).
 */
int socket_create(Socket* out, const netudp_address_t* bind_addr,
                  int send_buf_size, int recv_buf_size, int flags = 0);

/**
 * Send datagram to address. Returns bytes sent or -1 on error.
 */
int socket_send(Socket* sock, const netudp_address_t* dest,
                const void* data, int len);

/**
 * Receive datagram. Returns bytes received, 0 if no data (non-blocking), -1 on error.
 */
int socket_recv(Socket* sock, netudp_address_t* from,
                void* buf, int buf_len);

/**
 * Connect UDP socket to a remote address (caches route for send()).
 * For clients only — saves 1-3us/packet by avoiding route lookup per sendto.
 */
int socket_connect(Socket* sock, const netudp_address_t* dest);

/**
 * Send datagram on a connected socket (uses send() not sendto()).
 * Must call socket_connect() first. Returns bytes sent or -1.
 */
int socket_send_connected(Socket* sock, const void* data, int len);

/**
 * Close socket.
 */
void socket_destroy(Socket* sock);

/**
 * Receive up to max_pkts datagrams in a single call.
 *  pkts[i].data must point to a pre-allocated buffer of at least buf_len bytes.
 *  On return, pkts[i].addr = source address, pkts[i].len = bytes received.
 *  Linux: uses recvmmsg(). Windows/macOS: loops recvfrom().
 *  Returns count of packets received (0 = no data), -1 on error.
 */
int socket_recv_batch(Socket* sock, SocketPacket* pkts, int max_pkts, int buf_len);

/**
 * Send count datagrams in a single call.
 *  pkts[i].addr = destination, pkts[i].data = payload, pkts[i].len = bytes.
 *  Linux: uses sendmmsg(). Windows/macOS: loops sendto().
 *  Returns count of packets sent, -1 on error.
 */
int socket_send_batch(Socket* sock, const SocketPacket* pkts, int count);

} // namespace netudp

#endif /* NETUDP_SOCKET_H */
