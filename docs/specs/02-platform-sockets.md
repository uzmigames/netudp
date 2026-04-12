# Spec 02 — Platform Socket Abstraction

## Requirements

### REQ-02.1: Socket Types

```cpp
struct Socket {
    // Opaque platform handle (SOCKET on Windows, int on Unix)
    // IPv4 and IPv6 dual-stack
};

struct Address {
    union {
        uint8_t  ipv4[4];
        uint16_t ipv6[8];
    } data;
    uint16_t port;
    uint8_t  type;  // NETUDP_ADDRESS_NONE, _IPV4, _IPV6
};
```

`Address` SHALL be a POD struct of fixed size, suitable for use as hash map key.
`Address` SHALL be **zero-initialized on construction**. When `type == NETUDP_ADDRESS_IPV4`, the 12 bytes beyond `ipv4[4]` in the union MUST be zeroed to prevent spurious hash mismatches or false-inequality when used as hash map keys with SIMD byte comparison.
`Address` SHALL support equality comparison and hashing (SIMD-accelerated). Comparison SHALL compare only the bytes relevant to `type`: 4 bytes for IPv4, 16 bytes for IPv6, plus `port` and `type`. Hashing SHALL follow the same rule.

### REQ-02.2: Socket Operations

```cpp
namespace netudp::internal {

// Create non-blocking UDP socket, bind to address
// Returns error code
int socket_create(Socket* out, const Address* bind_addr, 
                  int send_buf_size, int recv_buf_size);

// Send datagram to address. Returns bytes sent or -1 on error.
int socket_send(Socket* sock, const Address* dest, 
                const void* data, int len);

// Receive datagram. Returns bytes received or 0 if no data, -1 on error.
int socket_recv(Socket* sock, Address* from, 
                void* buf, int buf_len);

// Close socket
void socket_destroy(Socket* sock);

// Batch send (Linux: sendmmsg, others: loop)
int socket_send_batch(Socket* sock, const SendPacket* packets, int count);

// Batch recv (Linux: recvmmsg, others: loop)
int socket_recv_batch(Socket* sock, RecvPacket* packets, int max_count);

}
```

### REQ-02.3: Platform Backends

| Platform | Create | Send | Recv | Batch Send | Batch Recv |
|---|---|---|---|---|---|
| Windows | `socket()` + `bind()` | `sendto()` | `recvfrom()` | Loop | Loop |
| Linux | `socket()` + `bind()` | `sendto()` | `recvfrom()` | `sendmmsg()` | `recvmmsg()` |
| macOS | `socket()` + `bind()` | `sendto()` | `recvfrom()` | Loop | Loop |

### REQ-02.4: Socket Options
All sockets SHALL be configured with:
- Non-blocking mode (`O_NONBLOCK` / `FIONBIO`)
- `SO_REUSEADDR` on the server socket
- `SO_SNDBUF` = 4 MB (configurable)
- `SO_RCVBUF` = 4 MB (configurable)
- IPv6 dual-stack: `IPV6_V6ONLY = 0` when binding to `::`

### REQ-02.5: Address Parsing

```cpp
// Parse "1.2.3.4:27015" or "[::1]:27015" into Address
int netudp_parse_address(const char* str, netudp_address_t* addr);

// Format Address to string
char* netudp_address_to_string(const netudp_address_t* addr, char* buf, int buf_len);

// Compare two addresses
int netudp_address_equal(const netudp_address_t* a, const netudp_address_t* b);
```

### REQ-02.6: DSCP Packet Tagging (Optional)
The socket MAY support setting DSCP/TOS bits for QoS prioritization.
Default: disabled.

```cpp
// Enable DSCP/TOS packet tagging on a socket.
// Must be called after socket_create() and before first send.
// dscp_value: 6-bit DSCP value (0-63), shifted into TOS byte bits 7-2.
// Returns 0 on success, -1 on error (platform not supported).
int netudp_enable_packet_tagging(netudp_server_t* server, uint8_t dscp_value);
```

## Scenarios

#### Scenario: Create and bind server socket
Given port 27015 is available
When `socket_create(&sock, &bind_addr, 4*1024*1024, 4*1024*1024)`
Then socket is created in non-blocking mode
And SO_SNDBUF >= 4MB
And SO_RCVBUF >= 4MB

#### Scenario: IPv6 dual-stack
Given binding to address "::" port 27015
When a client sends from IPv4 address 127.0.0.1
Then the server receives the packet with `from.type == NETUDP_ADDRESS_IPV4`

#### Scenario: Address parsing
Given string "192.168.1.100:7777"
When `netudp_parse_address(str, &addr)`
Then `addr.type == NETUDP_ADDRESS_IPV4`
And `addr.data.ipv4 == {192, 168, 1, 100}`
And `addr.port == 7777`
