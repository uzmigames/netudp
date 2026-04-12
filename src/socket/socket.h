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

/**
 * Create a non-blocking UDP socket, bind to address.
 * Sets SO_SNDBUF/SO_RCVBUF to send_buf_size/recv_buf_size.
 * IPv6 dual-stack: IPV6_V6ONLY=0 when addr is IPv6.
 */
int socket_create(Socket* out, const netudp_address_t* bind_addr,
                  int send_buf_size, int recv_buf_size);

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
 * Close socket.
 */
void socket_destroy(Socket* sock);

} // namespace netudp

#endif /* NETUDP_SOCKET_H */
