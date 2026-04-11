# 3. Integration with netudp

## How netc Fits in the netudp Stack

```
Application
    │ netudp_send(server, client, channel, data, len)
    ▼
┌─────────────────────────────────────────────┐
│ netudp Channel Layer                         │
│   Route to channel (reliable/unreliable)     │
├─────────────────────────────────────────────┤
│ netc Compression (OPTIONAL)                  │
│   Stateful ctx for reliable ordered channels │
│   Stateless for unreliable channels          │
├─────────────────────────────────────────────┤
│ netudp Reliability Layer                     │
│   Sequence, ACK, retransmit                  │
├─────────────────────────────────────────────┤
│ netudp Fragmentation                         │
│   Split if > MTU                             │
├─────────────────────────────────────────────┤
│ AEAD Encryption                              │
│   ChaCha20-Poly1305                          │
├─────────────────────────────────────────────┤
│ UDP Socket                                   │
└─────────────────────────────────────────────┘
```

**Compression happens BEFORE encryption** (unlike Server5 which compressed AFTER encryption). Compressing plaintext is always more effective than compressing ciphertext.

## Per-Channel Compression Contexts

```c
// For reliable ordered channel: stateful compression (best ratio)
netc_cfg_t cfg = {
    .flags = NETC_CFG_FLAG_STATEFUL | NETC_CFG_FLAG_DELTA | NETC_CFG_FLAG_COMPACT_HDR,
    .compression_level = 5,
};
netc_ctx_t *reliable_ctx = netc_ctx_create(dict, &cfg);

// For unreliable channel: stateless compression (no cross-packet dependency)
// Uses netc_compress_stateless() — no context needed
```

## Dictionary Deployment

The application trains a dictionary from representative packets during development:

```c
// During development/testing:
netc_dict_train(sample_packets, sizes, count, MODEL_ID, &dict);
netc_dict_save(dict, &blob, &blob_size);
// Save blob to file, ship with game

// At runtime:
netc_dict_load(blob_from_file, blob_size, &dict);
// Pass to netudp config
```

## netudp Configuration

```c
netudp_config_t config = netudp_default_config();
config.compression_dict = dict;           // NULL = no compression
config.compression_level = 5;             // 0-9
config.compression_flags = NETC_CFG_FLAG_DELTA | NETC_CFG_FLAG_COMPACT_HDR;
```

## Why Not LZ4?

Summary from benchmarks:

| Packet Size | LZ4 Ratio | netc Ratio | netc Savings vs LZ4 |
|---|---|---|---|
| 32 bytes | 1.056 (expands!) | 0.647 | **38% better** |
| 64 bytes | 0.875 | 0.758 | **13% better** |
| 128 bytes | 0.744 | 0.572 | **23% better** |
| 256 bytes | 0.478 | 0.331 | **31% better** |
| 512 bytes | 0.703 | 0.437 | **38% better** |

For typical game packets (32-256 bytes), netc saves **13-38% more bandwidth** than LZ4. For 32-byte packets, LZ4 actually makes things worse while netc saves 35%.

## Stateful vs Stateless Decision

| Channel Type | netc Mode | Why |
|---|---|---|
| Reliable Ordered | **Stateful** | Packets arrive in order → delta prediction works |
| Reliable Unordered | **Stateless** | Order not guaranteed → no cross-packet dependency |
| Unreliable | **Stateless** | Packets may be lost → no dependency on previous |
| Unreliable Sequenced | **Stateless** | Packets may be dropped → no dependency |

## Memory Budget

Per-connection overhead when compression enabled:
- Stateful context: ~64 KB (ring buffer) + ~3 KB (arena) = ~67 KB
- With adaptive mode: +16 KB = ~83 KB
- Dictionary (shared): ~32-128 KB (depends on training)
- Stateless: ~3 KB (arena only, allocated per-call from stack or pool)
