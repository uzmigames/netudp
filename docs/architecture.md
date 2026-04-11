# netudp — Architecture Document

## 0. Legacy Server Analysis

This architecture is informed by analysis of the ToS2 game server (C#/.NET), which ran in production with thousands of concurrent players. See [docs/analysis/server/](analysis/server/README.md) for the full analysis.

**Key patterns preserved from legacy:**
- Packet batching (BeginReliable/EndReliable) — critical for throughput
- Thread-local + global buffer pool — proven zero-alloc pattern
- Lock-free event queue (CAS-based intrusive linked list)
- Separate send thread with flush signaling
- CRC32C with hardware acceleration (SSE4.2 / ARM CRC32)
- VarInt + ZigZag encoding for compact serialization
- Symbol table / string interning for repeated strings
- Position delta + quantization

**Gaps in legacy that netudp fills:**
- Fragmentation & reassembly for large messages
- Multiple channel types (reliable unordered, unreliable sequenced)
- RTT-adaptive retransmission (legacy used fixed 150ms)
- Proper congestion control (legacy only had packet count limit)
- Connection challenge tokens (anti-spoof without full ECDH cost)
- Replay protection
- Statistics/diagnostics API

## 1. Design Principles

1. **Zero allocation in hot path** — all per-packet processing uses pre-allocated buffers from a pool. No `malloc` after initialization.
2. **Dual-thread model** — receive thread + send thread per server instance, matching the proven ToS2 architecture. Game logic runs on its own thread(s) and communicates via lock-free queues.
3. **Layered processing** — each packet passes through well-defined layers (socket → crypto → fragment → reliability → channel → application). Each layer is independently testable.
4. **Minimal copying** — direct pointer access for buffer I/O (proven in legacy). Headers are prepended via pointer arithmetic, not memcpy.
5. **Predictable memory** — all memory budgets are configured at creation time. The library never grows beyond its initial allocation.
6. **Packet batching** — multiple application messages are packed into single UDP packets up to MTU, exactly as the legacy server did with BeginReliable/EndReliable.

## 2. Layer Architecture

### 2.1 Socket Layer (`src/socket/`)

Platform abstraction over UDP sockets.

**Responsibilities:**
- Create/bind/close non-blocking UDP sockets
- `sendto` / `recvfrom` with scatter-gather (where platform supports)
- Socket option configuration (SO_REUSEADDR, SO_RCVBUF, SO_SNDBUF)
- Address resolution and comparison

**Platform backends:**
- Windows: Winsock2 (`WSARecvFrom` / `WSASendTo`)
- Linux: `recvmmsg` / `sendmmsg` for batch I/O
- macOS/BSD: standard `recvfrom` / `sendto`

**Key types:**
```c
typedef struct netudp_socket netudp_socket_t;

netudp_socket_t* netudp_socket_create(netudp_address_t* bind_addr);
int netudp_socket_send(netudp_socket_t* sock, netudp_address_t* dest, const void* data, size_t len);
int netudp_socket_recv(netudp_socket_t* sock, netudp_address_t* from, void* buf, size_t buf_len);
void netudp_socket_destroy(netudp_socket_t* sock);
```

### 2.2 Connection Manager (`src/connection/`)

Manages the lifecycle of virtual connections over UDP.

**State machine:**
```
DISCONNECTED → CONNECTING → CONNECTED → DISCONNECTING → DISCONNECTED
                    ↓                        ↓
               CHALLENGE_SENT           TIMED_OUT
```

**Connection handshake (3-way):**
1. Client → Server: `CONNECTION_REQUEST` (protocol version, client nonce)
2. Server → Client: `CONNECTION_CHALLENGE` (server nonce, challenge token = HMAC(client_nonce + server_nonce + client_addr, server_key))
3. Client → Server: `CONNECTION_RESPONSE` (challenge token echo, encryption keys derived from nonces)

This prevents IP spoofing (client must receive the challenge) and amplification attacks (server only allocates state after valid challenge response).

**Heartbeat:** Idle connections send heartbeat packets every `heartbeat_interval_ms` (default: 1000ms). If no packet is received for `timeout_ms` (default: 10000ms), the connection is dropped.

**Key types:**
```c
typedef struct {
    uint32_t id;                    // connection slot index
    netudp_conn_state_t state;
    netudp_address_t remote_addr;
    uint64_t connect_time_ms;
    uint64_t last_recv_time_ms;
    uint64_t last_send_time_ms;
    netudp_crypto_state_t crypto;   // per-connection encryption state
    netudp_channel_t channels[NETUDP_MAX_CHANNELS];
    netudp_bandwidth_t bandwidth;
} netudp_connection_t;
```

**Connection slot allocation:** Fixed-size array of `max_connections` slots. Free slots tracked via a free-list (no linear scan). Connection IDs are slot index + generation counter to detect stale references.

### 2.3 Channel Layer (`src/channel/`)

Each connection has up to `NETUDP_MAX_CHANNELS` (default: 4) logical channels. Channels define delivery semantics:

| Channel Type | Ordering | Reliability | Use Case |
|---|---|---|---|
| `UNRELIABLE` | None | None | Position updates, inputs |
| `UNRELIABLE_SEQUENCED` | Drop old | None | Latest-state snapshots |
| `RELIABLE_UNORDERED` | None | Retransmit | Chat, events |
| `RELIABLE_ORDERED` | Strict | Retransmit + buffer | RPCs, commands |

**Default channel assignment:**
- Channel 0: `RELIABLE_ORDERED` (control messages, RPCs)
- Channel 1: `UNRELIABLE` (game state updates)
- Channel 2: `RELIABLE_UNORDERED` (events, chat)
- Channel 3: `UNRELIABLE_SEQUENCED` (latest-state)

### 2.4 Reliability Engine (`src/reliability/`)

Implements selective acknowledgment for reliable channels.

**Packet header (reliability):**
```c
typedef struct {
    uint16_t sequence;          // packet sequence number (wraps at 65535)
    uint16_t ack;               // last received sequence from remote
    uint32_t ack_bits;          // bitmask of 32 packets before ack
} netudp_reliability_header_t;
```

**Mechanism:**
- Every sent packet gets a monotonically increasing 16-bit sequence number
- Every packet sent includes the latest ack + ack_bits for the remote
- `ack_bits` is a 32-bit bitmask: bit N = "I received packet (ack - N - 1)"
- Packets not acknowledged within `rtt * 1.5` are retransmitted (for reliable channels)
- Unreliable channels still use sequence numbers for ordering/staleness detection

**RTT estimation:** Exponential moving average with smoothing factor α=0.125, similar to TCP's SRTT calculation:
```
srtt = (1 - α) * srtt + α * sample_rtt
rttvar = (1 - β) * rttvar + β * |srtt - sample_rtt|
rto = srtt + 4 * rttvar
```

### 2.5 Fragmentation Layer (`src/fragment/`)

Handles messages larger than MTU.

**Design:**
- MTU discovery: start at 1200 bytes (safe for most paths), optionally probe up to 1400
- Messages ≤ MTU: sent as single packet
- Messages > MTU: split into fragments, each with a fragment header:

```c
typedef struct {
    uint16_t message_id;        // identifies the original message
    uint8_t  fragment_index;    // this fragment's index (0-based)
    uint8_t  fragment_count;    // total fragments in this message
} netudp_fragment_header_t;
```

- Maximum message size: 255 fragments × ~1200 bytes ≈ 300KB (configurable)
- Reassembly buffer holds partial messages; times out after `fragment_timeout_ms` (default: 5000ms)
- Fragment reassembly uses a bitmask to track received fragments — no ordering assumption

### 2.6 Encryption Layer (`src/crypto/`)

All traffic after handshake is encrypted with AEAD.

**Algorithm selection (compile-time):**
- Default: **ChaCha20-Poly1305** (fast on platforms without AES-NI)
- Optional: **AES-128-GCM** (fast on platforms with AES-NI)

**Key exchange during handshake:**
- Client and server exchange nonces during the 3-way handshake
- Shared key derived via: `key = HKDF(shared_secret, client_nonce || server_nonce, "netudp-session")`
- The `shared_secret` is a pre-shared key configured at server creation (connect tokens)

**Packet encryption:**
- Nonce: 64-bit packet counter (never reused per connection)
- Associated data: packet header (type, connection_id) — authenticated but not encrypted
- Payload: encrypted and authenticated

**Replay protection:** Sliding window of 256 most recent nonces. Packets with nonces older than the window or already seen are dropped.

### 2.7 Bandwidth Control (`src/connection/bandwidth.c`)

Per-connection send rate limiting.

**Token bucket algorithm:**
- Each connection has a token bucket with configurable rate (bytes/sec) and burst size
- Sending a packet consumes tokens equal to the packet size
- If insufficient tokens, the packet is queued until tokens are available
- Default rate: 256 KB/s per connection (configurable)
- Server-wide limit: sum of all connection rates capped at configured maximum

**Congestion avoidance:**
- Track packet loss rate from ack bitmasks
- If loss > 5%: reduce send rate by 25%
- If loss < 1% for 10 RTTs: increase send rate by 10% (up to configured max)
- Minimum rate: 32 KB/s (enough for heartbeats + essential game state)

## 3. Memory Model

### 3.1 Allocator

The library uses a configurable allocator interface:

```c
typedef struct {
    void* (*alloc)(size_t size, void* user_data);
    void  (*free)(void* ptr, void* user_data);
    void* user_data;
} netudp_allocator_t;
```

Default: `malloc`/`free`. Users can provide a custom allocator (arena, pool, etc.).

### 3.2 Buffer Pool

Pre-allocated pool of fixed-size packet buffers, created at server initialization:

```c
// Pool sizes determined at creation:
// - recv_pool: max_connections * 4 buffers
// - send_pool: max_connections * 4 buffers
// - fragment_pool: max_connections * max_fragments_per_connection
```

Buffer acquisition is O(1) via free-list. If the pool is exhausted, the send/recv operation returns `NETUDP_ERROR_NO_BUFFERS` (never falls back to malloc).

### 3.3 Memory Budget

For a server with 1024 connections:

| Component | Per-Connection | Total |
|---|---|---|
| Connection state | ~512 bytes | 512 KB |
| Reliability state | ~2 KB | 2 MB |
| Send buffer pool | 4 × 1400 bytes | 5.6 MB |
| Recv buffer pool | 4 × 1400 bytes | 5.6 MB |
| Fragment reassembly | ~64 KB | 64 MB |
| **Total** | | **~78 MB** |

Fragment reassembly dominates. If large messages are not needed, disabling fragmentation drops total to ~14 MB.

## 4. Packet Format

### 4.1 Wire Format

```
┌──────────────────────────────────────────────────┐
│ Byte 0: Packet Type (4 bits) | Flags (4 bits)   │
├──────────────────────────────────────────────────┤
│ Bytes 1-4: Connection ID (uint32)                │  ← Associated data
├──────────────────────────────────────────────────┤  ← (authenticated, not encrypted)
│ Bytes 5-12: Nonce (uint64)                       │
╞══════════════════════════════════════════════════╡
│ Reliability Header (6 bytes)                     │
│   sequence (uint16) | ack (uint16) | ack_bits    │
├──────────────────────────────────────────────────┤
│ Channel ID (uint8)                               │  ← Encrypted payload
├──────────────────────────────────────────────────┤
│ Fragment Header (4 bytes, optional)              │
├──────────────────────────────────────────────────┤
│ Payload Data (variable)                          │
╞══════════════════════════════════════════════════╡
│ Auth Tag (16 bytes)                              │  ← AEAD tag
└──────────────────────────────────────────────────┘
```

### 4.2 Packet Types

| Type | Value | Description |
|---|---|---|
| `CONNECTION_REQUEST` | 0x01 | Client → Server: initiate handshake |
| `CONNECTION_CHALLENGE` | 0x02 | Server → Client: challenge token |
| `CONNECTION_RESPONSE` | 0x03 | Client → Server: challenge response |
| `CONNECTION_DENIED` | 0x04 | Server → Client: connection refused |
| `DATA` | 0x05 | Application data (encrypted) |
| `HEARTBEAT` | 0x06 | Keep-alive |
| `DISCONNECT` | 0x07 | Graceful disconnect |

## 5. Public API Design

### 5.1 Core API (C interface)

```c
// --- Configuration ---
netudp_config_t netudp_default_config(void);

// --- Server ---
netudp_server_t*  netudp_server_create(const netudp_config_t* config);
void              netudp_server_destroy(netudp_server_t* server);
int               netudp_server_poll(netudp_server_t* server, netudp_event_t* event);
int               netudp_server_send(netudp_server_t* server, uint32_t conn_id,
                                     netudp_channel_type_t channel,
                                     const void* data, size_t len);
void              netudp_server_disconnect(netudp_server_t* server, uint32_t conn_id);
netudp_stats_t    netudp_server_stats(netudp_server_t* server);
netudp_conn_info_t netudp_server_connection_info(netudp_server_t* server, uint32_t conn_id);

// --- Client ---
netudp_client_t*  netudp_client_create(const netudp_config_t* config);
void              netudp_client_destroy(netudp_client_t* client);
int               netudp_client_connect(netudp_client_t* client, const char* host, uint16_t port);
int               netudp_client_poll(netudp_client_t* client, netudp_event_t* event);
int               netudp_client_send(netudp_client_t* client,
                                     netudp_channel_type_t channel,
                                     const void* data, size_t len);
void              netudp_client_disconnect(netudp_client_t* client);
netudp_stats_t    netudp_client_stats(netudp_client_t* client);
```

### 5.2 Event Types

```c
typedef enum {
    NETUDP_EVENT_NONE = 0,
    NETUDP_EVENT_CONNECT,       // new connection established
    NETUDP_EVENT_DISCONNECT,    // connection lost or closed
    NETUDP_EVENT_DATA,          // application data received
    NETUDP_EVENT_TIMEOUT,       // connection timed out
} netudp_event_type_t;

typedef struct {
    netudp_event_type_t type;
    uint32_t connection_id;
    netudp_channel_type_t channel;
    const void* data;           // valid only during this poll iteration
    size_t data_len;
} netudp_event_t;
```

### 5.3 Configuration

```c
typedef struct {
    uint16_t port;                      // bind port (0 = ephemeral)
    uint32_t max_connections;           // max simultaneous connections
    uint32_t heartbeat_interval_ms;     // heartbeat send interval (default: 1000)
    uint32_t timeout_ms;                // connection timeout (default: 10000)
    uint32_t max_bandwidth_bytes_sec;   // per-connection send rate limit
    uint16_t mtu;                       // initial MTU (default: 1200)
    uint8_t  max_channels;              // channels per connection (default: 4)
    bool     encryption_enabled;        // enable AEAD encryption (default: true)
    const uint8_t* private_key;         // 32-byte server private key (required if encryption enabled)
    netudp_allocator_t allocator;       // custom allocator (default: malloc/free)
    netudp_log_callback_t log_callback; // optional logging callback
} netudp_config_t;
```

## 6. Threading Model

**Single-threaded per instance.** Each `netudp_server_t` or `netudp_client_t` is designed to be used from a single thread. No internal locks.

**Multi-core scaling pattern:**
```
Thread 1: netudp_server_t* server1  (port 27015, connections 0-1023)
Thread 2: netudp_server_t* server2  (port 27016, connections 1024-2047)
Thread 3: netudp_server_t* server3  (port 27017, connections 2048-3071)
```

A load balancer or connection router assigns new connections to the least-loaded server instance. This is application-level — netudp itself is single-threaded per instance.

## 7. Error Handling

All functions return error codes. No exceptions (C interface). No global error state.

```c
typedef enum {
    NETUDP_OK = 0,
    NETUDP_ERROR_INVALID_PARAM = -1,
    NETUDP_ERROR_SOCKET = -2,
    NETUDP_ERROR_NO_BUFFERS = -3,
    NETUDP_ERROR_CONNECTION_FULL = -4,
    NETUDP_ERROR_NOT_CONNECTED = -5,
    NETUDP_ERROR_MESSAGE_TOO_LARGE = -6,
    NETUDP_ERROR_CRYPTO = -7,
    NETUDP_ERROR_TIMEOUT = -8,
} netudp_error_t;
```

## 8. Implementation Phases

### Phase 1: Foundation
- Platform socket abstraction (Windows + Linux)
- Buffer pool and allocator
- Basic send/recv (no reliability, no encryption)
- Connection handshake (simplified, no crypto)
- Google Test harness setup

### Phase 2: Reliability
- Sequence numbers and ack bitmask
- RTT estimation
- Reliable retransmission
- Channel types (unreliable, reliable ordered, reliable unordered)

### Phase 3: Fragmentation
- Message splitting and reassembly
- MTU configuration
- Fragment timeout and cleanup

### Phase 4: Security
- AEAD encryption (ChaCha20-Poly1305)
- Key derivation during handshake
- Replay protection
- Connect tokens

### Phase 5: Bandwidth & Polish
- Token bucket rate limiting
- Congestion avoidance
- Statistics and diagnostics API
- macOS support
- Examples and documentation

### Phase 6: Optimization
- `recvmmsg`/`sendmmsg` batch I/O on Linux
- SIMD-accelerated crypto (if not using hardware AES-NI)
- Memory layout optimization (cache-line alignment)
- Benchmarking suite
