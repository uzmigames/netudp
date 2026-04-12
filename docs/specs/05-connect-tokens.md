# Spec 05 — Connect Tokens & Handshake

## Requirements

### REQ-05.1: Connect Token Structure (2048 bytes)

**Public portion:**
```
Offset  Size   Field
0       13     version_info ("NETUDP 1.00\0")
13      8      protocol_id (uint64 LE)
21      8      create_timestamp (uint64 LE, unix seconds)
29      8      expire_timestamp (uint64 LE, unix seconds)
37      24     nonce (random, for XChaCha20)
61      1024   encrypted_private_data
1085    4      timeout_seconds (uint32 LE)
1089    4      num_server_addresses (uint32 LE, 1-32)
1093    var    server_addresses (type + addr + port each)
...     32     client_to_server_key
...     32     server_to_client_key
<zero pad to 2048>
```

**Private portion (before encryption):**
```
Offset  Size   Field
0       8      client_id (uint64 LE)
8       4      timeout_seconds (uint32 LE)
12      4      num_server_addresses (uint32 LE)
16      var    server_addresses
...     32     client_to_server_key
...     32     server_to_client_key
...     256    user_data (application-defined)
<zero pad to 1024>
```

Private encrypted with `xchacha20poly1305(private_key, nonce, aad)`.
AAD = version_info + protocol_id + expire_timestamp.

**Size verification:** The private data MUST fit within 1024 bytes before encryption. Worst case: 32 IPv6 addresses (19 bytes each = 608) + fixed fields (8+4+4+32+32+256 = 336) = 944 bytes + 16 bytes Poly1305 tag = 960 bytes encrypted. Maximum padding available: 64 bytes. This SHALL be verified at token generation time; `netudp_generate_connect_token()` SHALL return `NETUDP_ERROR_INVALID_PARAM` if `num_server_addresses > 32` or if the computed private data exceeds 1024 bytes pre-encryption.

### REQ-05.2: Token Generation

```cpp
int netudp_generate_connect_token(
    int            num_server_addresses,   // 1-32
    const char**   server_addresses,       // "1.2.3.4:27015" format
    int            expire_seconds,         // Token lifetime
    int            timeout_seconds,        // Connection timeout
    uint64_t       client_id,             // Unique per client
    uint64_t       protocol_id,           // Unique per game
    const uint8_t  private_key[32],       // Shared with server
    uint8_t        user_data[256],        // App data (can be NULL → zeros)
    uint8_t        connect_token[2048]    // Output
);
```

SHALL return `NETUDP_OK` on success, `NETUDP_ERROR_INVALID_PARAM` on bad input.

### REQ-05.3: 4-Step Handshake

**Step 1: CONNECTION_REQUEST (client → server, 1078 bytes)**
```
[0x00] [version(13)] [protocol_id(8)] [expire(8)] [nonce(24)] [encrypted_private(1024)]
```
Not encrypted. Fixed 1078 bytes.

**Step 2: CONNECTION_CHALLENGE (server → client, < 1078 bytes)**
```
[prefix] [seq(var)] [challenge_seq(8)] [encrypted_challenge_token(300)]
```
Challenge token encrypted with server's random key (regenerated at start).

**Step 3: CONNECTION_RESPONSE (client → server)**
```
[prefix] [seq(var)] [challenge_seq(8)] [encrypted_challenge_token(300)]
```
Client echoes challenge (proves it received step 2 at its claimed IP).

**Step 4: CONNECTION_KEEP_ALIVE (server → client)**
```
[prefix] [seq(var)] [client_index(4)] [max_clients(4)]
```
Client now CONNECTED.

### REQ-05.4: Server Validation (strict order)
1. Packet size != 1078 → ignore
2. Version mismatch → ignore
3. Protocol ID mismatch → ignore
4. Token expired → ignore
5. Private token decrypt fails → ignore
6. Server address not in token → ignore
7. Client address already connected → ignore
8. Client ID already connected → ignore
9. Token fingerprint already used from different IP → ignore
10. Record token fingerprint
11. Server full → send DENIED (< 1078 bytes, anti-amplification)
12. Add encryption mapping → send CHALLENGE

**Token Fingerprint (anti-replay):**

The server SHALL compute a fingerprint of each accepted connect token to prevent replay from different IPs:

```cpp
// Token fingerprint: HMAC-SHA256 truncated to 8 bytes
// Input: the encrypted_private_data (1024 bytes) from the CONNECTION_REQUEST
// Key: server's private_key (32 bytes)
struct TokenFingerprint {
    uint8_t hash[8];  // First 8 bytes of HMAC-SHA256(private_key, encrypted_private_data)
};
```

The server SHALL maintain a hash set of recently seen fingerprints (capacity = `max_connections * 4`, evict oldest on overflow). A fingerprint is recorded after step 10 with the client's source IP. If the same fingerprint arrives from a different IP, the packet is rejected (step 9). Fingerprints are evicted when the corresponding token expires (based on `expire_timestamp`).

**Note on CONNECTION_REQUEST:** The wire packet does NOT include `create_timestamp` (which exists only in the public token at offsets 21-28 for backend validation). The CONNECTION_REQUEST contains only: prefix(1) + version_info(13) + protocol_id(8) + expire_timestamp(8) + nonce(24) + encrypted_private(1024) = 1078 bytes.

### REQ-05.5: Client State Machine

```
States:
  -6  TOKEN_EXPIRED
  -5  INVALID_TOKEN
  -4  CONNECTION_TIMED_OUT
  -3  RESPONSE_TIMED_OUT
  -2  REQUEST_TIMED_OUT
  -1  CONNECTION_DENIED
   0  DISCONNECTED (initial)
   1  SENDING_REQUEST
   2  SENDING_RESPONSE
   3  CONNECTED (goal)
```

On failure in states 1 or 2: try next server in token. If no servers left → error state.

### REQ-05.6: Per-IP Rate Limiting

Before ANY token processing:
```cpp
struct TokenBucket {
    double tokens;
    int64_t last_refill_us;
    static constexpr int RATE = 60;   // packets/sec
    static constexpr int BURST = 10;

    void refill(int64_t now_us) {
        double elapsed = (now_us - last_refill_us) / 1e6;
        tokens = min(tokens + RATE * elapsed, (double)BURST);  // Cap at BURST
        last_refill_us = now_us;
    }

    bool try_consume() {
        refill(now());
        if (tokens >= 1.0) { tokens -= 1.0; return true; }
        return false;
    }
};
```

Initial `tokens` SHALL be set to `BURST` (10) on creation. `tokens` SHALL never exceed `BURST` (capped via `min()` in refill).

If bucket empty → silently drop packet. No response (prevents amplification).

### REQ-05.7: DDoS Escalation

```cpp
enum class DDoSSeverity : uint8_t { None, Low, Medium, High, Critical };

struct DDoSState {
    DDoSSeverity severity;
    int bad_packets_this_second;
    int escalation_threshold;
    double cooloff_timer;
};
```

Escalation rules:
- Low: bad_packets > 100/sec → log warning
- Medium: bad_packets > 500/sec → reduce processing budget
- High: bad_packets > 2000/sec → drop non-established packets
- Critical: bad_packets > 10000/sec → reject all new connections

Auto-cooloff after 30 seconds without escalation trigger.

## Scenarios

#### Scenario: Normal connection flow
Given a valid connect token for server A
When client sends CONNECTION_REQUEST to server A
Then server decrypts token, validates, sends CHALLENGE
When client sends RESPONSE
Then server assigns slot, sends KEEP_ALIVE
And client state = CONNECTED

#### Scenario: Expired token
Given a connect token with expire_timestamp in the past
When client sends CONNECTION_REQUEST
Then server ignores the packet (no response)

#### Scenario: Multi-server fallback
Given token with servers [A, B, C]
When server A responds with DENIED
Then client automatically tries server B
When server B responds with CHALLENGE
Then handshake continues with server B

#### Scenario: DDoS flood
Given 5000 bad packets/sec from various IPs
When DDoS monitor checks counters
Then severity escalates to High
And only packets from established connections are processed
When flood stops for 30 seconds
Then severity de-escalates to None
