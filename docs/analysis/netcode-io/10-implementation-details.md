# 10. Implementation Details

## Constants

```c
NETCODE_MAX_PAYLOAD_BYTES           1200    // Max app data per packet
NETCODE_MAX_PACKET_BYTES            1300    // Max on wire (payload + headers + MAC)
NETCODE_REPLAY_PROTECTION_BUFFER_SIZE 256   // Replay window size
NETCODE_MAX_CLIENTS                 256     // Max clients per server
NETCODE_PACKET_QUEUE_SIZE           256     // Internal packet queue
NETCODE_PACKET_SEND_RATE            10.0    // Keep-alive Hz
NETCODE_NUM_DISCONNECT_PACKETS      10      // Redundant disconnects
NETCODE_CLIENT_SOCKET_SNDBUF_SIZE   4MB     // Socket send buffer
NETCODE_CLIENT_SOCKET_RCVBUF_SIZE   4MB     // Socket recv buffer
NETCODE_CONNECT_TOKEN_BYTES         2048    // Connect token size
NETCODE_KEY_BYTES                   32      // Encryption key size
NETCODE_MAC_BYTES                   16      // AEAD tag size
NETCODE_USER_DATA_BYTES             256     // User data in token
```

## Socket Layer

Platform-specific: Winsock2 (Windows), BSD sockets (Mac/Linux). Non-blocking. IPv4 and IPv6 dual-stack. 4MB send/recv buffers. Optional DSCP packet tagging (QoS).

## Single-File Implementation

8680 lines in one `netcode.c`. Contains: platform sockets, crypto wrappers, connect/challenge tokens, all 7 packet types, client state machine, server management, replay protection, encryption manager, packet queue, network simulator, address map, connect token history, and full test suite.

## No Dependencies (besides sodium)

Vendors a stripped-down libsodium in `sodium/` directory. No other external dependencies. Pure C89/C99 compatible.

## Memory

Default: malloc/free. Custom allocator via config struct. No internal memory pools. Each received packet is individually allocated.
