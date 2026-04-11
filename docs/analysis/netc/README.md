# Analysis — netc (UzmiGames Network Compression Library)

Purpose-built network packet compression library. Will be used as netudp's compression layer instead of LZ4.

**Platform:** C11  
**Repository:** https://github.com/uzmigames/netc  
**License:** Apache 2.0  
**Version:** 0.2.0-dev  
**Analysis date:** 2026-04-11

---

## Why netc Instead of LZ4

| Aspect | LZ4 (Server5 approach) | netc |
|---|---|---|
| **Design target** | General-purpose fast compression | **Network packets specifically** |
| **32-byte packet** | Expands (ratio > 1.0) | **0.647** ratio (35% savings) |
| **64-byte packet** | 0.875 ratio | **0.758** ratio |
| **128-byte packet** | 0.744 ratio | **0.572** ratio (43% savings) |
| **256-byte packet** | 0.478 ratio | **0.331** ratio (67% savings!) |
| **512-byte packet** | 0.703 ratio | **0.437** ratio (56% savings) |
| **vs OodleNetwork UDP** | Not competitive | **Beats Oodle on all workloads** |
| Dictionary training | No | **Yes** — train from packet corpus |
| Stateful mode | No | **Yes** — cross-packet delta + adaptive tables |
| Stateless mode | Always | **Yes** — for unreliable/out-of-order packets |
| SIMD | No | **SSE4.2, AVX2, NEON** with runtime dispatch |
| Header overhead | N/A | **2-4 bytes** (compact mode) |
| Hot-path allocation | stackalloc hash table | **Zero** — pre-allocated arena |

LZ4 **expands** small packets (< 64 bytes) because its framing overhead exceeds savings. netc is purpose-built for exactly these packet sizes.

---

## Index

1. [Core Architecture](01-core-architecture.md)
2. [Algorithms](02-algorithms.md)
3. [Integration with netudp](03-integration.md)
