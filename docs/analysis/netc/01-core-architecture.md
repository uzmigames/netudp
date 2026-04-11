# 1. Core Architecture

## Design: Transport-Agnostic Compression Layer

netc sits between the application and the transport. It has **zero knowledge** of TCP, UDP, sockets, or any protocol. This makes it a perfect plug-in for netudp.

```
Application data
    │
    ▼
netc_compress(ctx, src, dst)     ← per-connection context
    │
    ▼
Compressed payload → netudp transport layer → encrypt → send
    │
    ▼ (receiver)
recv → decrypt → netc_decompress(ctx, src, dst)
    │
    ▼
Original application data
```

## Two Modes

### Stateful (for reliable ordered channels)
```c
// Context accumulates history across packets
netc_ctx_t *ctx = netc_ctx_create(dict, &cfg);  // cfg.flags = NETC_CFG_FLAG_STATEFUL
netc_compress(ctx, pkt1, ...);   // Uses ring buffer, delta from previous
netc_compress(ctx, pkt2, ...);   // Builds on pkt1's state
netc_compress(ctx, pkt3, ...);   // Builds on pkt2's state
```

### Stateless (for unreliable/unordered channels)
```c
// Each packet compressed independently — no shared state
netc_compress_stateless(dict, pkt, ...);  // No context needed
```

## Opaque Types

```c
netc_ctx_t  — per-connection compression context (one per thread)
netc_dict_t — trained dictionary (thread-safe, shared across connections)
```

## Dictionary Training

```c
// Train from representative packet corpus
netc_dict_train(packets, sizes, count, model_id, &dict);

// Save/load for deployment
netc_dict_save(dict, &blob, &blob_size);
netc_dict_load(blob, blob_size, &dict);
```

The dictionary contains:
- tANS probability tables (12-bit and 10-bit)
- LZP prediction hash table
- Bigram context class tables
- Per-position bucket assignments
- CRC32 checksum for validation

## Key Constants

```c
NETC_MAX_PACKET_SIZE   65535    // Max input size
NETC_MAX_OVERHEAD      8        // Max bytes added (header)
NETC_COMPACT_HDR_MIN   2        // Compact header for ≤127B packets
NETC_COMPACT_HDR_MAX   4        // Compact header for >127B packets
```

## Zero-Alloc Hot Path

All compression/decompression uses a pre-allocated arena (default 3000 bytes). No `malloc` during compress/decompress. Deterministic latency.

## SIMD Acceleration

Runtime dispatch to best available:
- **Generic** (scalar fallback, always available)
- **SSE4.2** (x86 with popcount, CRC)
- **AVX2** (x86 with 256-bit SIMD)
- **NEON** (ARM)

```c
uint8_t level = netc_ctx_simd_level(ctx);  // 1=generic, 2=sse42, 3=avx2, 4=neon
```

## Statistics

```c
netc_stats_t stats;
netc_ctx_stats(ctx, &stats);
// stats.packets_compressed, bytes_in, bytes_out, passthrough_count
```

## Passthrough Guarantee

If compression would **expand** the data, netc emits the original bytes with a passthrough flag. Output is **never** larger than `src_size + header_size`. This is critical for netudp — we never want compression to increase packet size.
