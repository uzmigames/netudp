# netudp — Architecture Document

## 0. Design Provenance

This architecture synthesizes the best patterns from six analyzed implementations:

| Source | What we adopt | Analysis |
|---|---|---|
| **ToS2/Server1** (C#, production) | Packet batching, thread-local buffer pool, lock-free CAS queues, CRC32C hw accel | [analysis/server/](analysis/server/README.md) |
| **ToS-Server-5** (C#, modern) | X25519 key exchange, ChaCha20-Poly1305, HMAC cookies, replay window, rekeying, token bucket | [analysis/server5/](analysis/server5/README.md) |
| **tos-mmorpg-server** (TS, WebSocket) | Confirmation that batching is universal, default-on encryption is critical | [analysis/mmorpg-server/](analysis/mmorpg-server/README.md) |
| **netcode.io** (C, Glenn Fiedler) | Connect token system, challenge/response handshake, opaque API, custom allocator, network simulator | [analysis/netcode-io/](analysis/netcode-io/README.md) |
| **Valve GNS** (C++, CS2/Dota2) | Ack vectors, multi-frame packets, Nagle batching, lanes with priority+weight, comprehensive stats, RTT from ack delay | [analysis/gns/](analysis/gns/README.md) |
| **netc** (C, UzmiGames) | Purpose-built network packet compression (tANS, LZP, delta prediction, SIMD). Replaces LZ4. | [analysis/netc/](analysis/netc/README.md) |

---

## 1. Design Principles

1. **Zero-GC, zero allocation in hot path.** The entire library is designed around **zero garbage collection**. All memory is pre-allocated at init (`netudp_server_start`). No `malloc`, `free`, `new`, `delete`, or any allocation during `update()`, `send()`, or `receive()`. Every buffer, every connection slot, every fragment tracker, every compression context — all pre-allocated from fixed pools. This is not just "minimal allocation" — it is **zero**. The library can run indefinitely without ever touching the system allocator after initialization. This is critical for game servers processing 100K+ packets/second where even a single allocation can cause frame spikes.

2. **Single-threaded per instance, no internal locks.** Each `netudp_server_t` / `netudp_client_t` is owned by one thread. The application calls `update()` to drive I/O. Matches netcode.io's explicit-time model and GNS's design. Multi-core scaling via multiple instances. No mutexes, no atomics, no lock contention in the hot path.

3. **Message-oriented, not stream-oriented.** Applications send discrete messages (like UDP), not byte streams (like TCP). The library handles reliability, ordering, and fragmentation transparently. Matches GNS's fundamental design.

4. **SDK-first design.** The C API is designed to be bound from **any language and engine**: C++ (native), C# (Unity, .NET), GDScript/C++ (Godot), Blueprint/C++ (Unreal), Rust, Go, Python. All types are POD structs. All functions use C linkage. No callbacks require closures. Opaque handles instead of raw pointers in public API.

5. **Packet interfaces for game servers.** The library provides clean callback interfaces and packet handler registration so game servers can plug in their own serialization, packet types, and game logic without modifying the transport. The transport is agnostic; the interfaces are not — they are purpose-built for game server architecture patterns.

6. **Encrypt everything by default.** All traffic after handshake uses AEAD encryption. CRC32C-only mode for LAN/dev via compile flag.

7. **Compress before encrypt.** Optional netc compression on plaintext before AEAD. Stateful per reliable channel, stateless per unreliable channel.

8. **Multi-frame packets.** A single UDP packet carries ack + stop-waiting + data frames (GNS SNP pattern). Nagle timer with per-message bypass.

9. **Pure C11, zero dependencies.** Vendored crypto (monocypher or libsodium subset). Optional netc for compression. Builds with `zig cc` for trivial cross-compilation to any target.

---

## 2. Layer Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│ Application                                                       │
│   netudp_server_send() / netudp_server_receive()                  │
│   netudp_client_send() / netudp_client_receive()                  │
├──────────────────────────────────────────────────────────────────┤
│ Channel Layer                                                     │
│   4 channel types × configurable count per connection             │
│   Priority + optional weight-based scheduling (from GNS lanes)    │
│   Per-channel Nagle timer (from GNS, configurable)                │
├──────────────────────────────────────────────────────────────────┤
│ Compression Layer (optional, via netc)                            │
│   Stateful for reliable ordered (delta + adaptive tANS)           │
│   Stateless for unreliable (independent per-packet)               │
│   Passthrough guarantee (never expands payload)                   │
├──────────────────────────────────────────────────────────────────┤
│ Reliability Engine                                                │
│   Sequence numbers (uint16) + ack + ack_bits (uint32)             │
│   RTT from ack delay (no separate ping, from GNS)                 │
│   Retransmission: RTT-adaptive with exponential backoff           │
│   Stop-waiting optimization (from GNS)                            │
├──────────────────────────────────────────────────────────────────┤
│ Fragmentation Layer                                               │
│   Split/reassemble for messages > MTU                             │
│   Fragment bitmask tracking, configurable timeout                 │
│   Max message: 64KB default, 512KB configurable (matches GNS)    │
├──────────────────────────────────────────────────────────────────┤
│ Encryption Layer (AEAD)                                           │
│   ChaCha20-Poly1305 (default) or AES-256-GCM (compile-time)      │
│   Sequence number as nonce (deterministic, from netcode.io)       │
│   Separate Tx/Rx keys (from netcode.io connect tokens)            │
│   256-entry replay protection window (from netcode.io)            │
│   Automatic rekeying at 1GB / 1h (from Server5)                   │
├──────────────────────────────────────────────────────────────────┤
│ Connection Manager                                                │
│   Connect token handshake (from netcode.io)                       │
│   Challenge/response anti-spoof                                   │
│   Token bucket rate limiting per IP (from Server5 WAF)            │
│   Connection slots with generation counter                        │
├──────────────────────────────────────────────────────────────────┤
│ Socket Layer                                                      │
│   Platform UDP abstraction (Win/Linux/Mac)                        │
│   recvmmsg/sendmmsg batch I/O on Linux                            │
│   4 MB send/recv socket buffers (from netcode.io)                 │
│   IPv4 + IPv6 dual-stack                                          │
│   Optional DSCP packet tagging (QoS, from netcode.io)             │
└──────────────────────────────────────────────────────────────────┘
```

---

## 3. Connect Token System (from netcode.io)

The authentication model follows netcode.io's proven connect token pattern exactly.

### 3.1 Architecture

```
┌──────────────┐      HTTPS       ┌──────────────┐
│ Web Backend   │ ◄──────────────► │   Client      │
│ (auth + REST) │                  │   (app)       │
└──────┬───────┘                  └──────┬───────┘
       │                                 │
       │ shared private_key              │ connect_token (2048 bytes)
       │                                 │
┌──────▼───────┐      UDP         ┌──────▼───────┐
│ Game Server   │ ◄──────────────► │   Client      │
│ (netudp)      │   encrypted      │   (netudp)    │
└──────────────┘                  └──────────────┘
```

### 3.2 Connect Token (2048 bytes)

**Public portion (readable by client):**
```
[version info]           13 bytes    "NETUDP 1.00\0"
[protocol id]            8 bytes     uint64 — unique per game
[create timestamp]       8 bytes     uint64 — unix timestamp
[expire timestamp]       8 bytes     uint64 — unix timestamp
[nonce]                  24 bytes    random (for XChaCha20 decryption)
[encrypted private data] 1024 bytes  server-only
[timeout seconds]        4 bytes     uint32
[num server addresses]   4 bytes     uint32 (1-32)
[server addresses]       variable    up to 32 addresses
[client to server key]   32 bytes    ChaCha20 key
[server to client key]   32 bytes    ChaCha20 key
<zero pad to 2048 bytes>
```

**Private portion (encrypted with shared private key):**
```
[client id]              8 bytes     uint64
[timeout seconds]        4 bytes     uint32
[server addresses]       variable    up to 32
[client to server key]   32 bytes
[server to client key]   32 bytes
[user data]              256 bytes   application-defined (player ID, matchmaking, etc.)
<zero pad to 1024 bytes>
```

### 3.3 Connection Handshake (4-step, from netcode.io)

```
Client                                     Server
  │                                           │
  │  1. CONNECTION_REQUEST (1078 bytes)       │
  │     [0x00][version][proto_id][expire]     │
  │     [nonce][encrypted_private_token]      │
  │ ─────────────────────────────────────────→│  Decrypt private token
  │                                           │  Validate version, proto_id, expire
  │                                           │  Check server addr in token
  │                                           │  Check no duplicate client/address
  │                                           │  Add encryption mapping
  │                                           │
  │  2. CONNECTION_CHALLENGE (< 1078 bytes)   │  Response < Request (anti-amplification)
  │     [prefix][seq][challenge_seq]          │
  │     [encrypted_challenge_token]           │
  │ ←─────────────────────────────────────────│
  │                                           │
  │  3. CONNECTION_RESPONSE                   │
  │     [prefix][seq][challenge_seq]          │
  │     [encrypted_challenge_token]           │
  │ ─────────────────────────────────────────→│  Decrypt challenge token
  │                                           │  Assign client to slot
  │                                           │
  │  4. CONNECTION_KEEP_ALIVE                 │
  │     [prefix][seq][client_index]           │
  │     [max_clients]                         │
  │ ←─────────────────────────────────────────│
  │                                           │
  │  Client is CONNECTED                      │
  │  All subsequent traffic AEAD encrypted    │
```

**Anti-DDoS properties:**
- Server allocates NO per-client state until valid token decrypted (step 1)
- Challenge/response proves client can receive at claimed IP (step 2-3)
- Response always smaller than request (no amplification)
- Token HMAC tracked to prevent reuse from different IPs
- Token bucket rate limit per source IP before token processing

### 3.4 Client State Machine (from netcode.io)

```
Error states (negative):
  -6  connect_token_expired
  -5  invalid_connect_token
  -4  connection_timed_out
  -3  connection_response_timed_out
  -2  connection_request_timed_out
  -1  connection_denied

Normal states:
   0  disconnected (initial)
   1  sending_connection_request
   2  sending_connection_response
   3  connected (goal)
```

Multi-server fallback: if server N fails, client tries server N+1 from token.

---

## 4. Channel System

### 4.1 Channel Types

| Channel Type | Ordering | Reliability | Typical Use |
|---|---|---|---|
| `NETUDP_CHANNEL_UNRELIABLE` | None | None | Position updates, inputs |
| `NETUDP_CHANNEL_UNRELIABLE_SEQUENCED` | Drop stale | None | Latest-state snapshots |
| `NETUDP_CHANNEL_RELIABLE_ORDERED` | Strict | ACK + retransmit | RPCs, commands, chat |
| `NETUDP_CHANNEL_RELIABLE_UNORDERED` | None | ACK + retransmit | Events, asset requests |

### 4.2 Channel Configuration

```c
typedef struct {
    netudp_channel_type_t type;
    int priority;           // Lower = higher priority (strict ordering between priorities)
    uint16_t weight;        // Weight within same priority (weighted fair queue, from GNS)
    uint32_t nagle_time_us; // Nagle timer in microseconds (0 = disabled)
} netudp_channel_config_t;
```

**Default channels:**
```
Channel 0: RELIABLE_ORDERED,       priority=0, weight=1, nagle=5000us  (control)
Channel 1: UNRELIABLE,             priority=1, weight=3, nagle=0       (game state)
Channel 2: RELIABLE_UNORDERED,     priority=1, weight=1, nagle=5000us  (events)
Channel 3: UNRELIABLE_SEQUENCED,   priority=2, weight=1, nagle=0       (latest-state)
```

### 4.3 Priority + Weight Scheduling (from GNS)

```
Priority 0 (control): always sent first when bandwidth available
Priority 1 (game):    3/4 bandwidth to unreliable (weight=3), 1/4 to reliable (weight=1)
Priority 2 (bulk):    only when higher priorities are empty
```

### 4.4 Per-Channel Compression (via netc)

```
RELIABLE_ORDERED     → netc stateful  (delta + adaptive — best ratio)
RELIABLE_UNORDERED   → netc stateless (independent packets)
UNRELIABLE           → netc stateless (no cross-packet dependency)
UNRELIABLE_SEQUENCED → netc stateless (packets may be dropped)
```

---

## 5. Reliability Engine

### 5.1 Packet-Level Acknowledgment

Every data packet carries piggybacked ack information (from GNS/netcode.io hybrid):

```c
typedef struct {
    uint16_t sequence;       // This packet's sequence number
    uint16_t ack;            // Latest received sequence from remote
    uint32_t ack_bits;       // Bitmask: bit N = received (ack - N - 1)
    uint16_t ack_delay_us;   // Microseconds since receiving 'ack' (for RTT, from GNS)
} netudp_packet_header_t;
```

**Every packet is an implicit ping.** The `ack_delay_us` field means no separate ping/pong packets are needed (GNS pattern). RTT is continuously measured from every ack.

### 5.2 RTT Estimation (TCP-style SRTT)

```
sample_rtt = (now - send_time_of_acked_packet) - ack_delay_us
srtt       = (1 - α) × srtt + α × sample_rtt           (α = 0.125)
rttvar     = (1 - β) × rttvar + β × |srtt - sample_rtt| (β = 0.25)
rto        = srtt + 4 × rttvar
```

### 5.3 Reliable Retransmission

- Packets not acknowledged within `rto` are retransmitted
- Exponential backoff: `rto × 2^retry_count` up to `rto_max` (default: 2000ms)
- Maximum retries: 10 (from Server5, vs Server1's 30)
- After max retries: message dropped, connection quality tracked

### 5.4 Stop-Waiting (from GNS)

Periodically, a stop-waiting value is sent: "I have received all packets before sequence X." This allows the sender to stop tracking old packets and shrink the ack window.

### 5.5 Replay Protection (from netcode.io)

```c
#define NETUDP_REPLAY_BUFFER_SIZE 256

struct netudp_replay_protection {
    uint64_t most_recent_sequence;
    uint64_t received_packet[256];  // Stores actual sequence at each index
};
```

256-entry window using netcode.io's `uint64_t` array approach (more robust than bitmask).

---

## 6. Wire Format

### 6.1 Multi-Frame Packet (from GNS)

A single UDP packet contains multiple **frames**. This maximizes information density.

```
┌──────────────────────────────────────────────────────┐
│ Packet Header (authenticated, not encrypted)          │
│   [1B prefix: type(4) | seq_bytes(4)]                │
│   [1-8B sequence number (variable-length)]            │
│   [4B connection_id]                                  │
╞══════════════════════════════════════════════════════╡
│ Encrypted Payload (AEAD)                              │
│ ┌──────────────────────────────────────────────────┐ │
│ │ Frame: ACK                                       │ │
│ │   [ack(2B)][ack_bits(4B)][ack_delay(2B)]         │ │
│ ├──────────────────────────────────────────────────┤ │
│ │ Frame: Stop-Waiting (optional)                   │ │
│ │   [offset from current seq]                      │ │
│ ├──────────────────────────────────────────────────┤ │
│ │ Frame: Channel Data (repeated per message)       │ │
│ │   [channel_id(1B)][msg_size(var)][payload]       │ │
│ ├──────────────────────────────────────────────────┤ │
│ │ Frame: Channel Data ...                          │ │
│ └──────────────────────────────────────────────────┘ │
╞══════════════════════════════════════════════════════╡
│ AEAD Tag (16 bytes)                                   │
└──────────────────────────────────────────────────────┘
```

### 6.2 Frame Types

```
0x01  ACK frame         — piggybacked acknowledgments
0x02  Stop-waiting      — advance sender's window
0x03  Unreliable data   — channel_id + message data
0x04  Reliable data     — channel_id + sequence + message data
0x05  Fragment          — fragment_id + index + count + data
0x06  Disconnect        — graceful close (sent redundantly)
```

### 6.3 Packet Types (Unencrypted, Handshake Only)

```
0x00  CONNECTION_REQUEST    — 1078 bytes, contains encrypted connect token
0x01  CONNECTION_DENIED     — minimal, no state allocated
0x02  CONNECTION_CHALLENGE  — challenge token (< request size)
0x03  CONNECTION_RESPONSE   — challenge echo
```

### 6.4 Variable-Length Sequence (from netcode.io)

```
Prefix byte high 4 bits = number of sequence bytes (1-8)
Prefix byte low 4 bits = packet type

Sequence 1000 (0x3E8): needs 2 bytes → prefix high bits = 2
Written as: 0xE8, 0x03 (little-endian)
```

Saves 6 bytes per packet for the first ~65K packets.

### 6.5 Associated Data for AEAD

```
[version info]   13 bytes    "NETUDP 1.00\0"
[protocol id]    8 bytes     uint64
[prefix byte]    1 byte      prevents packet type modification
```

Matches netcode.io's AAD scheme. The header is authenticated but not encrypted — routers can see connection ID for load balancing, but content is protected.

---

## 7. Encryption

### 7.1 Algorithms

| Purpose | Algorithm | Source |
|---|---|---|
| Connect token encryption | XChaCha20-Poly1305 (24B nonce) | netcode.io |
| Packet encryption | ChaCha20-Poly1305 (12B nonce) | netcode.io / Server5 |
| Challenge token | ChaCha20-Poly1305 (server key) | netcode.io |
| Key exchange | Pre-shared via connect token | netcode.io |
| Optional compile-time | AES-256-GCM (for AES-NI platforms) | GNS |

### 7.2 Nonce Construction (from netcode.io)

```c
// 12-byte nonce = sequence number zero-padded to 96 bits
uint8_t nonce[12] = {0};
memcpy(nonce, &sequence, sizeof(uint64_t));  // LE
```

Deterministic — no random generation needed. Unique because sequence never repeats.

### 7.3 Key Management

- **Client → Server key** and **Server → Client key** are separate (from connect token)
- Prevents reflection attacks
- Keys derived offline by web backend, embedded in connect token
- No online key exchange needed (cheaper than ECDH)

### 7.4 Automatic Rekeying (from Server5)

```c
#define NETUDP_REKEY_BYTES_THRESHOLD  (1ULL << 30)  // 1 GB
#define NETUDP_REKEY_TIME_THRESHOLD   3600           // 1 hour

// When either threshold exceeded:
// 1. Generate new keys via HKDF(old_keys, "rekey" || max_seq)
// 2. Reset sequence counters and replay window
// 3. Both sides derive independently (deterministic)
```

### 7.5 CRC32C Fast Path (from Server1)

For LAN/development scenarios where encryption overhead is unwanted:

```c
// Compile with -DNETUDP_INSECURE_MODE
// Replaces AEAD with CRC32C integrity check (no confidentiality)
// Hardware accelerated: SSE4.2 (x86), CRC32 (ARM)
// WARNING: Not for production over untrusted networks
```

---

## 8. Compression (via netc)

### 8.1 Integration

```
Application message
    │
    ▼ (per-channel compression context)
netc_compress(ctx, plaintext) → compressed payload
    │
    ▼
AEAD encrypt(compressed payload) → ciphertext + tag
    │
    ▼
UDP sendto()
```

### 8.2 Performance (netc vs LZ4 vs nothing)

| Packet Size | No compression | LZ4 | **netc** | netc savings |
|---|---|---|---|---|
| 32 bytes | 32B | 34B (expands!) | **21B** | 35% |
| 64 bytes | 64B | 56B | **48B** | 25% |
| 128 bytes | 128B | 95B | **73B** | 43% |
| 256 bytes | 256B | 122B | **85B** | 67% |

### 8.3 Configuration

```c
netudp_config_t config = netudp_default_config();
config.compression_dict = netc_dict;  // NULL = no compression
config.compression_level = 5;         // 0=fastest, 9=best ratio
```

---

## 9. Bandwidth Control

### 9.1 Token Bucket (from Server5 WAF + GNS)

```c
typedef struct {
    uint32_t rate_bytes_per_sec;   // Token refill rate
    uint32_t burst_bytes;          // Maximum burst size
    double   tokens;               // Current available tokens
    uint64_t last_refill_time_us;  // Last refill timestamp
} netudp_token_bucket_t;
```

### 9.2 Congestion Control

Loss-based congestion avoidance:
- Track packet loss rate from ack bitmask
- Loss > 5%: reduce send rate by 25%
- Loss < 1% for 10 RTTs: increase send rate by 10% (up to configured max)
- Minimum rate: 32 KB/s (heartbeats + essential data)

### 9.3 Send Rate Estimation (from GNS)

The library estimates channel capacity and exposes it in statistics:

```c
stats.send_rate_bytes_per_sec  // Estimated capacity
stats.queue_time_us            // How long data waits before being sent
```

---

## 10. Connection Statistics (from GNS)

```c
typedef struct {
    // Timing
    uint32_t ping_ms;                    // Current RTT
    float    connection_quality_local;   // 0..1 (packet delivery rate)
    float    connection_quality_remote;  // 0..1 (as seen by remote)

    // Throughput
    float    out_packets_per_sec;
    float    out_bytes_per_sec;
    float    in_packets_per_sec;
    float    in_bytes_per_sec;
    uint32_t send_rate_bytes_per_sec;    // Estimated channel capacity

    // Queue depth
    uint32_t pending_unreliable_bytes;
    uint32_t pending_reliable_bytes;
    uint32_t sent_unacked_reliable_bytes;
    uint64_t queue_time_us;              // Estimated wait before send

    // Compression (if netc enabled)
    float    compression_ratio;          // bytes_out / bytes_in
    uint64_t bytes_saved;                // Total bytes saved by compression

    // Per-channel stats
    netudp_channel_stats_t channels[NETUDP_MAX_CHANNELS];
} netudp_connection_stats_t;
```

---

## 11. Public API

### 11.1 Lifecycle

```c
// Initialize / terminate (global, once)
int  netudp_init(void);
void netudp_term(void);

// Server
netudp_server_t * netudp_server_create(const char * address, const netudp_server_config_t * config, double time);
void              netudp_server_start(netudp_server_t * server, int max_clients);
void              netudp_server_stop(netudp_server_t * server);
void              netudp_server_update(netudp_server_t * server, double time);
void              netudp_server_destroy(netudp_server_t * server);

// Client
netudp_client_t * netudp_client_create(const char * address, const netudp_client_config_t * config, double time);
void              netudp_client_connect(netudp_client_t * client, uint8_t * connect_token);
void              netudp_client_update(netudp_client_t * client, double time);
void              netudp_client_disconnect(netudp_client_t * client);
void              netudp_client_destroy(netudp_client_t * client);
```

### 11.2 Send / Receive

```c
// Send (per connection, per channel, with flags)
int netudp_server_send(netudp_server_t * server, int client_index,
                       int channel, const void * data, int bytes, int flags);

int netudp_client_send(netudp_client_t * client,
                       int channel, const void * data, int bytes, int flags);

// Batch send (multiple messages to multiple connections, from GNS)
void netudp_server_send_messages(netudp_server_t * server,
                                 int count, netudp_message_t * messages,
                                 int64_t * results);

// Receive (returns array of messages, from GNS)
int netudp_server_receive(netudp_server_t * server, int client_index,
                          netudp_message_t ** messages, int max_messages);

int netudp_client_receive(netudp_client_t * client,
                          netudp_message_t ** messages, int max_messages);

// Free received messages
void netudp_message_release(netudp_message_t * message);

// Flush (bypass Nagle, send immediately)
void netudp_server_flush(netudp_server_t * server, int client_index);
void netudp_client_flush(netudp_client_t * client);
```

### 11.3 Send Flags (from GNS)

```c
#define NETUDP_SEND_UNRELIABLE       0   // Fire-and-forget (default)
#define NETUDP_SEND_RELIABLE         1   // Guaranteed delivery
#define NETUDP_SEND_NO_NAGLE         2   // Skip Nagle batching delay
#define NETUDP_SEND_NO_DELAY         4   // Skip Nagle + send immediately
```

### 11.4 Messages

```c
typedef struct {
    void *       data;              // Payload pointer
    int          size;              // Payload size
    int          channel;           // Channel index
    int          client_index;      // Which client (server-side)
    int          flags;             // NETUDP_SEND_* flags
    int64_t      message_number;    // Sequence for this message
    uint64_t     receive_time_us;   // When received (microseconds)
    void         (*release)(struct netudp_message_t *);  // Release function
} netudp_message_t;
```

### 11.5 Connect Token Generation (from netcode.io)

```c
int netudp_generate_connect_token(
    int            num_server_addresses,
    const char **  server_addresses,
    int            expire_seconds,
    int            timeout_seconds,
    uint64_t       client_id,
    uint64_t       protocol_id,
    const uint8_t  private_key[32],
    uint8_t        user_data[256],
    uint8_t        connect_token[2048]
);
```

### 11.6 Configuration

```c
typedef struct {
    uint64_t protocol_id;                       // Unique per game
    uint8_t  private_key[32];                   // Shared with web backend
    void *   allocator_context;                 // Custom allocator user data
    void *   (*allocate_function)(void *, size_t);
    void     (*free_function)(void *, void *);
    void *   callback_context;                  // Callback user data
    void     (*connect_disconnect_callback)(void *, int client_index, int connected);
    netudp_channel_config_t channels[NETUDP_MAX_CHANNELS];
    int      num_channels;                      // Default: 4
    const netc_dict_t * compression_dict;       // NULL = no compression
    uint8_t  compression_level;                 // 0-9 (default: 5)
    int      log_level;                         // NETUDP_LOG_LEVEL_*
    void     (*log_callback)(int level, const char * msg);
} netudp_server_config_t;
```

### 11.7 Statistics & Diagnostics

```c
// Per-connection stats
int netudp_server_connection_status(netudp_server_t * server, int client_index,
                                    netudp_connection_stats_t * stats);

// Per-channel stats within a connection
int netudp_server_channel_status(netudp_server_t * server, int client_index,
                                 int channel, netudp_channel_stats_t * stats);

// Global server stats
int netudp_server_stats(netudp_server_t * server, netudp_server_stats_t * stats);
```

---

## 12. Zero-GC Memory Model

### 12.1 Zero-GC Guarantee

**After `netudp_server_start()` returns, the library will NEVER call `malloc`, `free`, `realloc`, `calloc`, or any system allocator function.** This is a hard guarantee, not a best-effort.

All memory is allocated in two phases:

```
Phase 1: CREATE (allocates)
  netudp_server_create()  → allocates server struct
  netudp_server_start()   → pre-allocates ALL pools, ALL per-connection state

Phase 2: RUNTIME (zero-alloc)
  netudp_server_update()  → uses only pre-allocated pools
  netudp_server_send()    → acquires buffer from pool (O(1) free-list pop)
  netudp_server_receive() → returns pointer into recv pool (zero-copy)
  netudp_message_release()→ returns buffer to pool (O(1) free-list push)

Phase 3: DESTROY (frees)
  netudp_server_stop()    → returns all pool memory
  netudp_server_destroy() → frees server struct
```

**Why this matters for game servers:**
- No GC pauses (C# Unity, Java, Go all suffer from this)
- No heap fragmentation over 24/7 server uptime
- Deterministic frame timing (no allocation jitter)
- Cache-friendly memory layout (pools are contiguous)
- Safe to run on embedded/console platforms with no virtual memory

### 12.2 Custom Allocator (from netcode.io)

The allocator is **only called during Phase 1 and Phase 3**. During runtime, all allocation goes through internal pools.

```c
typedef struct {
    void * context;
    void * (*alloc)(void * context, size_t bytes);
    void   (*free)(void * context, void * ptr);
} netudp_allocator_t;
```

This allows engines to route all netudp memory through their own allocator (Unreal's FMemory, Unity's UnsafeUtility, Godot's memalloc, etc.).

### 12.3 Pool Architecture

```
netudp_server_t
  │
  ├── packet_pool ──────── Fixed array of netudp_packet_t [max_clients × 16]
  │                        O(1) acquire/release via intrusive free-list
  │                        Each packet: MTU_SIZE bytes (1400)
  │
  ├── message_pool ─────── Fixed array of netudp_message_t [max_clients × 64]
  │                        Returned to application via receive()
  │                        Application calls release() to return to pool
  │
  ├── fragment_pool ────── Fixed array of netudp_fragment_t [max_clients × max_frags]
  │                        Reassembly buffers for large messages
  │
  ├── connection_pool ──── Fixed array of netudp_connection_t [max_clients]
  │                        Includes: crypto state, channel state, reliability state
  │                        Slot reuse via generation counter
  │
  └── compression_pool ─── Fixed array of netc_ctx_t [max_clients × num_stateful_channels]
                           (only if compression enabled)
                           One stateful context per reliable ordered channel per connection
```

### 12.4 Memory Budget (1024 connections, 4 channels)

| Component | Per-Connection | Total |
|---|---|---|
| Connection state + crypto | ~1 KB | 1 MB |
| Reliability state (seq, acks, window) | ~3 KB | 3 MB |
| Packet pool (16 × 1400B) | 22 KB | 22 MB |
| Message pool (64 × 64B descriptors) | 4 KB | 4 MB |
| Fragment reassembly (64 KB max msg) | 64 KB | 64 MB |
| Compression context (netc, optional) | 67 KB | 67 MB |
| Channel state (4 channels) | ~2 KB | 2 MB |
| **Total (no compression, no fragmentation)** | | **~32 MB** |
| **Total (with fragmentation)** | | **~96 MB** |
| **Total (with fragmentation + netc)** | | **~163 MB** |

All allocated once at start. Zero-alloc during runtime. Configurable per use case.

---

## 13. Threading Model

**Single-threaded per instance. No internal locks.**

```c
// Application thread owns the instance:
netudp_server_t * server = netudp_server_create("0.0.0.0:27015", &config, time);
netudp_server_start(server, 256);

while (running) {
    double time = get_time();
    netudp_server_update(server, time);  // Drives all I/O

    // Receive
    netudp_message_t * msgs[64];
    for (int i = 0; i < 256; i++) {
        int n = netudp_server_receive(server, i, msgs, 64);
        for (int j = 0; j < n; j++) {
            process(msgs[j]);
            netudp_message_release(msgs[j]);
        }
    }

    // Send
    netudp_server_send(server, client_idx, 0, data, len, NETUDP_SEND_RELIABLE);

    sleep_until_next_tick();
}
```

**Multi-core scaling:** multiple server instances on different ports/threads. Application-level load balancing routes new connections to least-loaded instance.

---

## 14. Network Simulator (from netcode.io)

Built-in for testing. Configurable per-instance:

```c
typedef struct {
    float latency_ms;        // Added latency
    float jitter_ms;         // Random ± jitter
    float packet_loss_pct;   // 0..100
    float duplicate_pct;     // 0..100
} netudp_network_simulator_config_t;
```

---

## 15. Packet Interfaces (for Game Server Implementation)

netudp is transport-only, but it provides clean interfaces so game servers can plug in packet handling, serialization, and dispatch without modifying the library.

### 15.1 Packet Handler Interface

```c
/**
 * Application registers handlers per packet type.
 * The transport calls these when data arrives on a channel.
 * Handler receives raw bytes — application deserializes.
 */
typedef void (*netudp_packet_handler_fn)(
    void *            user_context,   // Application state (game world, player controller)
    int               client_index,   // Which client sent this
    int               channel,        // Which channel it arrived on
    const uint8_t *   data,           // Payload pointer (into recv pool, zero-copy)
    int               data_len,       // Payload length
    uint64_t          sequence        // Message sequence number
);

/**
 * Register a handler for a specific application packet type.
 * packet_type: first byte(s) of payload, application-defined.
 * If no handler registered, data is delivered via generic receive().
 */
void netudp_server_set_packet_handler(
    netudp_server_t *       server,
    uint16_t                packet_type,  // Application-defined type ID
    netudp_packet_handler_fn handler,
    void *                  user_context
);
```

### 15.2 Connection Lifecycle Callbacks

```c
typedef struct {
    /** Called when a new client connects. Return user_data to associate with this connection. */
    void * (*on_connect)(void * context, int client_index, uint64_t client_id,
                         const uint8_t user_data[256]);

    /** Called when a client disconnects (timeout, graceful, or kicked). */
    void (*on_disconnect)(void * context, int client_index, void * conn_user_data,
                          int reason);

    /** Called every server update tick. Use for game logic tied to network tick. */
    void (*on_tick)(void * context, double time, double dt);

    /** Application context passed to all callbacks. */
    void * context;
} netudp_server_callbacks_t;
```

### 15.3 Write Buffer Interface (Zero-Copy Send)

```c
/**
 * Acquire a write buffer from the pool (zero-alloc).
 * Write your packet data into it, then send.
 * This is the Server1 BeginReliable/EndReliable pattern adapted for C.
 */
netudp_buffer_t * netudp_server_acquire_buffer(netudp_server_t * server);

// Write helpers (application-level, in include/netudp/buffer.h)
void netudp_buffer_write_u8(netudp_buffer_t * buf, uint8_t value);
void netudp_buffer_write_u16(netudp_buffer_t * buf, uint16_t value);
void netudp_buffer_write_u32(netudp_buffer_t * buf, uint32_t value);
void netudp_buffer_write_u64(netudp_buffer_t * buf, uint64_t value);
void netudp_buffer_write_f32(netudp_buffer_t * buf, float value);
void netudp_buffer_write_varint(netudp_buffer_t * buf, int32_t value);
void netudp_buffer_write_bytes(netudp_buffer_t * buf, const void * data, int len);
void netudp_buffer_write_string(netudp_buffer_t * buf, const char * str, int max_len);

// Read helpers (for deserialization)
uint8_t  netudp_buffer_read_u8(netudp_buffer_t * buf);
uint16_t netudp_buffer_read_u16(netudp_buffer_t * buf);
uint32_t netudp_buffer_read_u32(netudp_buffer_t * buf);
float    netudp_buffer_read_f32(netudp_buffer_t * buf);
int32_t  netudp_buffer_read_varint(netudp_buffer_t * buf);
int      netudp_buffer_read_bytes(netudp_buffer_t * buf, void * out, int max_len);
int      netudp_buffer_read_string(netudp_buffer_t * buf, char * out, int max_len);

// Send the buffer (returns it to pool after batching/sending)
int netudp_server_send_buffer(netudp_server_t * server, int client_index,
                              int channel, netudp_buffer_t * buf, int flags);
```

### 15.4 Example: Game Server Packet Handling

```c
// Application-defined packet types (NOT part of netudp)
enum GamePacketType {
    GAME_PKT_MOVE           = 0x01,
    GAME_PKT_ATTACK         = 0x02,
    GAME_PKT_CHAT           = 0x03,
    GAME_PKT_ENTITY_UPDATE  = 0x10,
    // ... game-specific
};

// Handler for movement packets
void handle_move(void * ctx, int client, int ch, const uint8_t * data, int len, uint64_t seq) {
    GameWorld * world = (GameWorld *)ctx;
    netudp_buffer_t buf = { .data = (uint8_t*)data, .size = len, .position = 0 };

    float x = netudp_buffer_read_f32(&buf);
    float y = netudp_buffer_read_f32(&buf);
    float z = netudp_buffer_read_f32(&buf);

    game_world_move_player(world, client, x, y, z);
}

// Registration
netudp_server_set_packet_handler(server, GAME_PKT_MOVE, handle_move, game_world);
netudp_server_set_packet_handler(server, GAME_PKT_ATTACK, handle_attack, game_world);
```

### 15.5 Broadcast / Multicast Helpers

```c
/** Send to all connected clients on a channel. */
void netudp_server_broadcast(netudp_server_t * server, int channel,
                             const void * data, int bytes, int flags);

/** Send to all clients except one (e.g., broadcast entity update to everyone except owner). */
void netudp_server_broadcast_except(netudp_server_t * server, int except_client,
                                    int channel, const void * data, int bytes, int flags);

/** Send to a list of clients (e.g., players in same zone/area of interest). */
void netudp_server_send_to_list(netudp_server_t * server, const int * client_indices,
                                int count, int channel, const void * data, int bytes, int flags);
```

---

## 16. SDK & Engine Integration

### 16.1 Target Engines

| Engine | Binding Language | Integration Pattern |
|---|---|---|
| **UzmiGames Custom Engine** | C (native) | Direct API calls |
| **Unreal Engine 5** | C++ | Thin C++ wrapper over C API, `TSharedPtr` for lifetime |
| **Unity** | C# (P/Invoke) | `[DllImport("netudp")]` flat C API, `NativeArray<byte>` for buffers |
| **Godot 4** | C (GDExtension) | GDExtension C API, `PackedByteArray` for buffers |
| **Rust** | Rust (FFI) | `extern "C"` bindings, `unsafe` wrapper with safe Rust API |
| **Go** | Go (cgo) | `#cgo` directives, Go wrapper types |

### 16.2 C API Design for FFI Compatibility

All public types follow strict rules for bindability:

```c
// ✅ POD structs only (no function pointers in data types passed across FFI)
typedef struct {
    uint32_t ping_ms;
    float    quality;
} netudp_connection_stats_t;

// ✅ Opaque handles (not raw pointers) for lifetime safety
typedef struct netudp_server netudp_server_t;  // Forward declaration only

// ✅ All functions use C linkage
#ifdef __cplusplus
extern "C" {
#endif

// ✅ Fixed-size arrays, not pointers-to-arrays
int netudp_generate_connect_token(
    ...,
    uint8_t connect_token[2048]  // Fixed size, not uint8_t*
);

// ✅ Error codes as int return values (not exceptions, not errno)
int netudp_server_send(...);  // Returns NETUDP_OK or NETUDP_ERROR_*

// ✅ Callbacks use void* context (not closures/lambdas)
void (*on_connect)(void * context, int client_index, ...);
```

### 16.3 Platform Libraries

```
netudp/
├── lib/
│   ├── win-x64/      netudp.dll, netudp.lib
│   ├── win-arm64/    netudp.dll, netudp.lib
│   ├── linux-x64/    libnetudp.so, libnetudp.a
│   ├── linux-arm64/  libnetudp.so, libnetudp.a
│   ├── macos-x64/    libnetudp.dylib, libnetudp.a
│   ├── macos-arm64/  libnetudp.dylib, libnetudp.a
│   ├── android-arm64/ libnetudp.so
│   ├── ios-arm64/    libnetudp.a
│   └── wasm/         netudp.wasm (WebRTC fallback, future)
├── include/
│   └── netudp/
│       ├── netudp.h          // Core API (server, client, send, recv)
│       ├── netudp_types.h    // All public types and constants
│       ├── netudp_buffer.h   // Buffer read/write helpers
│       ├── netudp_token.h    // Connect token generation
│       └── netudp_config.h   // Compile-time configuration
└── sdk/
    ├── unreal/       // UE5 plugin (C++ wrapper + .uplugin)
    ├── unity/        // Unity package (C# bindings + .asmdef)
    ├── godot/        // GDExtension (C wrapper + .gdextension)
    └── cpp/          // C++ header-only wrapper (RAII, std::span)
```

### 16.4 Unreal Engine Integration Example

```cpp
// NetudpSubsystem.h (UE5)
UCLASS()
class UNetudpSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

    netudp_server_t * Server = nullptr;

    virtual void Initialize(FSubsystemCollectionBase& Collection) override {
        netudp_init();
        netudp_server_config_t Config = {};
        netudp_default_server_config(&Config);
        Config.allocate_function = FMemoryAllocator;  // Route to UE allocator
        Server = netudp_server_create("0.0.0.0:7777", &Config, FPlatformTime::Seconds());
        netudp_server_start(Server, 64);
    }

    virtual void Deinitialize() override {
        netudp_server_destroy(Server);
        netudp_term();
    }

    void Tick(float DeltaTime) {
        netudp_server_update(Server, FPlatformTime::Seconds());
        // Receive and process messages...
    }
};
```

### 16.5 Unity Integration Example

```csharp
// NetudpServer.cs (Unity)
public class NetudpServer : IDisposable {
    [DllImport("netudp")] static extern int netudp_init();
    [DllImport("netudp")] static extern IntPtr netudp_server_create(string addr, ref ServerConfig cfg, double time);
    [DllImport("netudp")] static extern void netudp_server_update(IntPtr server, double time);
    [DllImport("netudp")] static extern int netudp_server_send(IntPtr server, int client,
        int channel, byte[] data, int bytes, int flags);
    // ... other bindings

    IntPtr _server;

    public void Start(int maxClients) {
        netudp_init();
        var cfg = ServerConfig.Default();
        _server = netudp_server_create("0.0.0.0:7777", ref cfg, Time.realtimeSinceStartupAsDouble);
        netudp_server_start(_server, maxClients);
    }

    // Called from MonoBehaviour.Update() — zero GC, no managed allocations
    public void Tick() {
        netudp_server_update(_server, Time.realtimeSinceStartupAsDouble);
    }

    public void Dispose() {
        netudp_server_destroy(_server);
        netudp_term();
    }
}
```

### 16.6 Godot Integration Example

```gdscript
# netudp_server.gd (Godot 4, GDExtension)
extends Node

var server: NetudpServer

func _ready():
    server = NetudpServer.new()
    server.start("0.0.0.0:7777", 64)
    server.connect("client_connected", _on_client_connected)
    server.connect("client_disconnected", _on_client_disconnected)
    server.connect("packet_received", _on_packet_received)

func _process(delta):
    server.update(Time.get_unix_time_from_system())

func _on_packet_received(client_index: int, channel: int, data: PackedByteArray):
    var packet_type = data.decode_u8(0)
    match packet_type:
        GamePacket.MOVE:
            handle_move(client_index, data)
```

---

## 17. Implementation Phases

### Phase 0: Project Setup
- CMake + Zig CC build system
- Google Test harness
- CI (GitHub Actions: Windows + Linux + macOS)
- Public header structure (`include/netudp/*.h`)
- Coding style, static analysis (clang-tidy)

### Phase 1: Foundation
- Platform socket abstraction (Windows + Linux + macOS)
- Buffer pool and custom allocator
- Address parsing, comparison, IPv4/IPv6
- Basic non-blocking send/recv
- Google Test harness
- CMake + Zig CC build system

### Phase 2: Connection + Encryption
- Connect token generation and validation
- 4-step handshake (request → challenge → response → connected)
- ChaCha20-Poly1305 AEAD (vendored libsodium subset)
- Replay protection (256-entry window)
- Separate Tx/Rx keys
- Client state machine with multi-server fallback
- Token bucket rate limiting per IP

### Phase 3: Reliability + Channels
- Sequence numbers + piggybacked ack + ack_bits
- RTT estimation from ack delay (no ping/pong)
- RTT-adaptive retransmission with exponential backoff
- 4 channel types (unreliable, unreliable sequenced, reliable ordered, reliable unordered)
- Priority + weight scheduling
- Nagle timer with per-message bypass
- Stop-waiting optimization

### Phase 4: Fragmentation + Large Messages
- Message splitting at MTU boundary
- Fragment bitmask tracking and reassembly
- Configurable max message size (default 64KB, up to 512KB)
- Fragment timeout and cleanup

### Phase 5: Compression + Statistics
- netc integration (stateful + stateless per channel type)
- Dictionary loading from config
- Comprehensive connection statistics (GNS-level)
- Per-channel statistics
- Automatic rekeying

### Phase 6: Packet Interfaces + Game Server Support
- Packet handler registration API
- Connection lifecycle callbacks (on_connect, on_disconnect, on_tick)
- Write buffer acquire/send (zero-copy pattern)
- Buffer read/write helpers (u8, u16, u32, f32, varint, string)
- Broadcast / multicast helpers
- Example game server (echo, entity sync, chat)

### Phase 7: Optimization + Polish
- `recvmmsg`/`sendmmsg` batch I/O on Linux
- Batch send/receive API
- Network simulator
- DSCP packet tagging
- Memory layout optimization (cache-line alignment)
- Benchmarking suite (pps, latency, memory)
- Profiling under load (1000+ connections)

### Phase 8: SDK & Engine Bindings
- C++ header-only wrapper (RAII, std::span, std::function)
- Unreal Engine 5 plugin (.uplugin + C++ subsystem)
- Unity package (C# P/Invoke bindings + NativeArray integration)
- Godot 4 GDExtension (C binding + GDScript-friendly API)
- Cross-compile all platform libs via Zig CC
- Platform testing matrix (Win/Linux/Mac × x64/ARM64)
- SDK documentation and quick-start guides per engine
