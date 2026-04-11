# Spec 11 — Compression (via netc)

## Requirements

### REQ-11.1: Integration

netc is an optional dependency. Enabled via `NETUDP_ENABLE_COMPRESSION` CMake option.
When disabled, all compression paths are compiled out (zero overhead).

```cpp
// Config
struct netudp_server_config_t {
    const netc_dict_t* compression_dict;  // NULL = no compression
    uint8_t            compression_level; // 0-9 (default: 5)
};
```

### REQ-11.2: Compression Pipeline Position

```
App message → Channel queue → [COMPRESS] → Reliability → Fragment → [ENCRYPT] → Socket
```

Compression happens BEFORE encryption (plaintext compresses far better than ciphertext).
Compression happens BEFORE fragmentation (compress whole message, then fragment if needed).

### REQ-11.3: Per-Channel Mode

| Channel Type | netc Mode | Context | Why |
|---|---|---|---|
| ReliableOrdered | Stateful | 1 ctx per channel per connection | Packets in order → delta prediction works |
| ReliableUnordered | Stateless | Shared | Order not guaranteed |
| Unreliable | Stateless | Shared | Packets may be lost |
| UnreliableSequenced | Stateless | Shared | Packets may be dropped |

### REQ-11.4: Passthrough Guarantee

netc SHALL never expand a payload. If compression would produce output larger than input, the original bytes are emitted with a passthrough flag. The wire format frame includes a 1-bit `compressed` flag.

### REQ-11.5: Context Lifecycle

```cpp
// Per connection, per reliable ordered channel:
netc_ctx_t* ctx = netc_ctx_create(dict, &stateful_cfg);

// On connection established:
// Context already pre-allocated from compression_pool

// On connection reset/disconnect:
netc_ctx_reset(ctx);  // Reset state, keep memory

// On server stop:
netc_ctx_destroy(ctx);  // Release memory
```

### REQ-11.6: Memory

Per stateful context: ~67 KB (ring buffer 64KB + arena 3KB).
Pre-allocated in `compression_pool` during `server_start()`.

## Scenarios

#### Scenario: Compression improves bandwidth
Given 128-byte game state update packet
When compressed with trained dictionary
Then output ≤ 73 bytes (ratio 0.57 from netc benchmarks)
And compressed flag set in frame header

#### Scenario: Incompressible data passthrough
Given 32-byte random data
When compression attempted
Then output = original 32 bytes + passthrough flag
And no expansion occurs

#### Scenario: Stateful cross-packet delta
Given reliable ordered channel with stateful compression
When second packet is similar to first (typical game state)
Then compression ratio improves due to delta prediction
