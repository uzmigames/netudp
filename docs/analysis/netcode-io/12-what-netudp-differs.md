# 12. What netudp Does Differently from netcode.io

## netudp = netcode.io + reliable.io + more

netcode.io is deliberately minimal — connection + encryption only. It's designed to pair with reliable.io for reliability. **netudp combines both into one library**, plus adds features neither provides.

## Features netudp Adds

| # | Feature | netcode.io | netudp |
|---|---------|-----------|--------|
| 1 | **Reliability** | None | ACK bitmask + retransmission (RTT-adaptive) |
| 2 | **Ordering** | None | Reliable ordered + reliable unordered |
| 3 | **Channels** | 1 (unreliable) | 4+ (unreliable, unreliable sequenced, reliable ordered, reliable unordered) |
| 4 | **Fragmentation** | None (1200 max) | Split/reassemble for large messages |
| 5 | **Packet batching** | None | Multiple messages per UDP packet |
| 6 | **Compression** | None | Optional LZ4 |
| 7 | **Bandwidth control** | None | Token bucket + congestion avoidance |
| 8 | **Statistics API** | None | RTT, packet loss, bandwidth, per-connection metrics |
| 9 | **Buffer pool** | malloc per packet | Pre-allocated pool (zero-alloc hot path) |
| 10 | **Rekeying** | None | Automatic (bytes/time threshold) |
| 11 | **CRC32C fast-path** | None (always AEAD) | CRC32C for non-encrypted mode (dev/LAN) |
| 12 | **AES-GCM option** | ChaCha20 only | ChaCha20 (default) + AES-GCM (compile-time for AES-NI) |

## Architectural Differences

| Aspect | netcode.io | netudp |
|---|---|---|
| Threading | Single-threaded only | Single-threaded per instance (same), but with optional send thread |
| Packet receive | Returns one packet at a time | Event-based poll (multiple per call) |
| Send model | Immediate send | Batched send (flush at end of tick) |
| Memory model | malloc/free per operation | Pre-allocated pools |
| Build system | premake5 | CMake + Zig CC |
| Crypto | Vendored libsodium | Vendored or minimal implementation |

## Why Not Just Use netcode.io + reliable.io?

1. **Two libraries = integration complexity** — connection state must be shared between netcode.io and reliable.io manually
2. **No batching** — each reliable message = one UDP packet. With 20+ messages per tick, that's 20+ packets instead of 1-2
3. **No compression** — for bandwidth-constrained scenarios
4. **No bandwidth control** — no congestion avoidance, no rate limiting
5. **No statistics** — no way to query connection quality
6. **License** — netcode.io is BSD, netudp is Apache 2.0 (more permissive for some use cases)

## What netudp Should NOT Copy

1. **256-client hard limit** — netudp should support configurable limits (1024+)
2. **No batching** — netudp batches by default (proven in Server1)
3. **Prefix byte encoding** — clever but we have a proper 14-byte header (from Server5)
4. **Single-file implementation** — 8680 lines in one .c file is hard to maintain. netudp uses a layered source tree.
