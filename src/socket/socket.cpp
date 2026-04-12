#include "socket.h"
#include <cstring>

namespace netudp {

/* ===== Platform init/term ===== */

#if defined(NETUDP_PLATFORM_WINDOWS)

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

static void address_to_sockaddr(const netudp_address_t* addr,
                                 struct sockaddr_storage* ss, int* ss_len) {
    std::memset(ss, 0, sizeof(*ss));

    if (addr->type == NETUDP_ADDRESS_IPV6) {
        auto* sin6 = reinterpret_cast<struct sockaddr_in6*>(ss);
        sin6->sin6_family = AF_INET6;
        sin6->sin6_port = htons(addr->port);
        for (int i = 0; i < 8; ++i) {
            sin6->sin6_addr.s6_addr[i * 2]     = static_cast<uint8_t>(addr->data.ipv6[i] >> 8);
            sin6->sin6_addr.s6_addr[i * 2 + 1] = static_cast<uint8_t>(addr->data.ipv6[i] & 0xFF);
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
            addr->data.ipv6[i] = static_cast<uint16_t>(
                (sin6->sin6_addr.s6_addr[i * 2] << 8) |
                 sin6->sin6_addr.s6_addr[i * 2 + 1]);
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
                  int send_buf_size, int recv_buf_size) {
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

    /* Buffer sizes */
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
#if defined(NETUDP_PLATFORM_WINDOWS)
    unsigned long nonblock = 1;
    if (ioctlsocket(sock, FIONBIO, &nonblock) != 0) {
        closesocket(sock);
        return NETUDP_ERROR_SOCKET;
    }
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        close(sock);
        return NETUDP_ERROR_SOCKET;
    }
#endif

    /* Bind */
    struct sockaddr_storage ss;
    int ss_len = 0;
    address_to_sockaddr(bind_addr, &ss, &ss_len);

    if (bind(sock, reinterpret_cast<const struct sockaddr*>(&ss), ss_len) != 0) {
#if defined(NETUDP_PLATFORM_WINDOWS)
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
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET) {
        return -1;
    }

    struct sockaddr_storage ss;
    int ss_len = 0;
    address_to_sockaddr(dest, &ss, &ss_len);

    int result = sendto(sock->handle,
                        static_cast<const char*>(data), len, 0,
                        reinterpret_cast<const struct sockaddr*>(&ss), ss_len);

#if defined(NETUDP_PLATFORM_WINDOWS)
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
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET) {
        return -1;
    }

    struct sockaddr_storage ss;
    std::memset(&ss, 0, sizeof(ss));

#if defined(NETUDP_PLATFORM_WINDOWS)
    int ss_len = sizeof(ss);
#else
    socklen_t ss_len = sizeof(ss);
#endif

    int result = recvfrom(sock->handle,
                          static_cast<char*>(buf), buf_len, 0,
                          reinterpret_cast<struct sockaddr*>(&ss), &ss_len);

#if defined(NETUDP_PLATFORM_WINDOWS)
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

void socket_destroy(Socket* sock) {
    if (sock == nullptr || sock->handle == NETUDP_INVALID_SOCKET) {
        return;
    }

#if defined(NETUDP_PLATFORM_WINDOWS)
    closesocket(sock->handle);
#else
    close(sock->handle);
#endif

    sock->handle = NETUDP_INVALID_SOCKET;
}

} // namespace netudp
