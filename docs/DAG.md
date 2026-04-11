# netudp — Dependency Graph (DAG)

Task dependency graph for implementation. Each node can only start when ALL its dependencies are complete.

---

## Dependency Visualization

```
                            ┌──────────────┐
                            │  P0: Project  │
                            │    Setup      │
                            └──────┬───────┘
                                   │
                    ┌──────────────┼──────────────┐
                    ▼              ▼              ▼
             ┌────────────┐ ┌──────────┐ ┌──────────────┐
             │ P1-A: SIMD │ │ P1-B:    │ │ P1-C: Buffer │
             │  Detect +  │ │ Platform │ │  Pool + Alloc│
             │  Dispatch  │ │ Sockets  │ │              │
             └─────┬──────┘ └────┬─────┘ └──────┬───────┘
                   │             │              │
                   │        ┌────┴────┐         │
                   │        ▼         │         │
                   │  ┌──────────┐    │         │
                   │  │ P1-D:    │    │         │
                   │  │ Address  │    │         │
                   │  │ Parsing  │    │         │
                   │  └────┬─────┘    │         │
                   │       │          │         │
                   ▼       ▼          ▼         ▼
             ┌─────────────────────────────────────┐
             │        P1-E: Basic Send/Recv         │
             │    (socket + buffer pool + address)   │
             └──────────────────┬──────────────────┘
                                │
              ┌─────────────────┼─────────────────┐
              ▼                 ▼                 ▼
     ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
     │ P2-A: Crypto │  │ P2-B: Connect│  │ P2-C: Replay │
     │  Vendored    │  │  Token Gen + │  │  Protection  │
     │  ChaCha20    │  │  Validate    │  │  (256 window)│
     │  + Poly1305  │  │              │  │              │
     └──────┬───────┘  └──────┬───────┘  └──────┬───────┘
            │                 │                 │
            ▼                 ▼                 │
     ┌─────────────────────────────┐            │
     │ P2-D: AEAD Encrypt/Decrypt  │            │
     │  (packet-level, seq nonce)  │            │
     └──────────────┬──────────────┘            │
                    │                           │
                    ▼                           ▼
     ┌──────────────────────────────────────────────┐
     │    P2-E: 4-Step Handshake                     │
     │   (request → challenge → response → connected)│
     │   + Client State Machine                      │
     │   + Per-IP Rate Limiting                      │
     └──────────────────────┬───────────────────────┘
                            │
              ┌─────────────┼─────────────┐
              ▼             ▼             ▼
     ┌──────────────┐ ┌──────────┐ ┌──────────────┐
     │ P3-A: Packet │ │ P3-B: RTT│ │ P3-C: Channel│
     │  Sequence +  │ │ from Ack │ │  Types + Nagle│
     │  Ack Bitmask │ │ Delay    │ │  + Priority   │
     └──────┬───────┘ └────┬─────┘ └──────┬───────┘
            │              │              │
            ▼              ▼              │
     ┌──────────────────────────┐         │
     │ P3-D: Dual-Layer          │         │
     │  Reliability (packet +    │◄────────┘
     │  per-channel message seq) │
     │  + Retransmission         │
     │  + Stop-Waiting           │
     └──────────────┬───────────┘
                    │
              ┌─────┴─────┐
              ▼           ▼
     ┌──────────────┐ ┌──────────────┐
     │ P4-A:        │ │ P4-B: Multi- │
     │ Fragment     │ │ Frame Packet │
     │ Split +      │ │ Assembly     │
     │ Reassemble   │ │ (ack + data  │
     │ + Bitmask    │ │  in 1 packet)│
     └──────┬───────┘ └──────┬───────┘
            │                │
            ▼                ▼
     ┌──────────────────────────────┐
     │ P4-C: Full Pipeline           │
     │  send → compress → fragment → │
     │  encrypt → batch → socket     │
     └──────────────┬───────────────┘
                    │
     ┌──────────────┼──────────────┐
     ▼              ▼              ▼
┌──────────┐ ┌──────────────┐ ┌──────────────┐
│ P5-A:    │ │ P5-B: netc   │ │ P5-C: DDoS   │
│ Bandwidth│ │ Compression  │ │ Escalation   │
│ Control  │ │ Integration  │ │ (5 severity  │
│ (AIMD +  │ │ (stateful +  │ │  levels)     │
│ token    │ │  stateless)  │ │              │
│ bucket)  │ │              │ │              │
└────┬─────┘ └──────┬───────┘ └──────┬───────┘
     │              │                │
     ▼              ▼                ▼
┌─────────────────────────────���─────────────┐
│ P5-D: Connection Statistics (GNS-level)    │
│  ping, quality, throughput, queue, compress│
└──────────────────┬────────────────────────┘
                   │
     ┌─────────────┼─────────────┐
     ▼             ▼             ▼
┌──────────┐ ┌──────────────┐ ┌──────────────┐
│ P6-A:    │ │ P6-B: Packet │ │ P6-C: Auto   │
│ Network  │ │ Interfaces + │ │ Rekeying     │
│ Simulator│ │ Callbacks +  │ │              │
│          │ │ Buffer API   │ │              │
└────┬─────┘ └──────┬───────┘ └──────┬───────┘
     │              │                │
     ▼              ▼                ▼
┌────────────────────────────────���──────────┐
│ P6-D: Benchmark Suite                      │
│  PPS, latency, SIMD compare, scalability   │
└──────────────────┬────────────────────────┘
                   │
     ┌─────────────┼──────────────────┐
     ▼             ▼                  ▼
┌──────────┐ ┌──────────────┐ ┌──────────────┐
│ P7-A:    │ │ P7-B: Batch  │ │ P7-C:        │
│ recvmmsg │ │ Send/Recv    │ │ Examples     │
│ sendmmsg │ │ API          │ │ (echo, chat, │
│ (Linux)  │ │              │ │  stress)     │
└────┬─────┘ └──────┬───────┘ └──────┬───────┘
     │              │                │
     ▼              ▼                ▼
┌───────────────────────────────────────────┐
│ P7-D: v1.0 Release                         │
│  All tests pass, benchmarks meet targets   │
└──────────────────┬────────────────────────┘
                   │
     ┌─────────────┼──────────────────┐
     ▼             ▼                  ▼
┌──────────┐ ┌──────────────┐ ┌──────────────┐
│ P8-A:    │ │ P8-B: Unreal │ │ P8-C: Unity  │
│ C++ RAII │ │ Plugin       │ │ C# Bindings  │
│ Wrapper  │ │              │ │              │
└──────────┘ └──────────────┘ └──────────────┘
                                     │
                              ┌──────┴───────┐
                              ▼              ▼
                       ┌──────────┐  ┌──────────────┐
                       │ P8-D:    │  │ P8-E:        │
                       │ Godot    │  │ UzEngine     │
                       │ GDExt    │  │ Subsystem    │
                       └──────────┘  └──────────────┘
```

---

## Task List (Topological Order)

### Phase 0 — Project Setup (P0)
| ID | Task | Dependencies | Est. |
|---|---|---|---|
| P0.1 | CMakeLists.txt root + presets | — | 2h |
| P0.2 | Zig CC toolchain integration | P0.1 | 2h |
| P0.3 | Google Test via FetchContent | P0.1 | 1h |
| P0.4 | CI: GitHub Actions (Win + Linux + Mac) | P0.1 | 3h |
| P0.5 | Public header structure (`include/netudp/`) | P0.1 | 1h |
| P0.6 | clang-tidy + clang-format config | P0.1 | 1h |
| P0.7 | `netudp_init()` / `netudp_term()` stubs | P0.5 | 1h |

### Phase 1 — Foundation (P1)
| ID | Task | Dependencies | Est. |
|---|---|---|---|
| P1-A.1 | SIMD detection (CPUID x86, compile-time ARM) | P0.7 | 4h |
| P1-A.2 | Dispatch table (`netudp_simd_ops_t`) | P1-A.1 | 2h |
| P1-A.3 | Generic scalar implementations | P1-A.2 | 4h |
| P1-A.4 | SSE4.2 implementations | P1-A.2 | 6h |
| P1-A.5 | AVX2 implementations | P1-A.2 | 6h |
| P1-A.6 | NEON implementations | P1-A.2 | 6h |
| P1-B.1 | Socket abstraction (create, bind, close) | P0.7 | 4h |
| P1-B.2 | Windows Winsock2 backend | P1-B.1 | 4h |
| P1-B.3 | Linux/Mac BSD socket backend | P1-B.1 | 4h |
| P1-B.4 | Non-blocking mode + poll | P1-B.2, P1-B.3 | 2h |
| P1-B.5 | Socket buffer sizes (4MB) | P1-B.4 | 1h |
| P1-C.1 | Custom allocator interface | P0.7 | 2h |
| P1-C.2 | `Pool<T>` template (free-list, zero-alloc) | P1-C.1 | 4h |
| P1-C.3 | `FixedRingBuffer<T,N>` | P0.7 | 3h |
| P1-C.4 | `FixedHashMap<K,V,N>` (open addressing) | P0.7 | 4h |
| P1-D.1 | Address parsing (IPv4, IPv6, "host:port") | P0.7 | 3h |
| P1-D.2 | Address comparison + hashing | P1-D.1 | 2h |
| P1-E.1 | Basic send/recv (raw UDP, no encryption) | P1-B.4, P1-C.2, P1-D.2 | 4h |
| P1-E.2 | Integration test: echo server | P1-E.1 | 2h |

### Phase 2 — Connection + Encryption (P2)
| ID | Task | Dependencies | Est. |
|---|---|---|---|
| P2-A.1 | Vendor monocypher (or libsodium subset) | P0.7 | 3h |
| P2-A.2 | ChaCha20-Poly1305 AEAD wrapper | P2-A.1 | 4h |
| P2-A.3 | XChaCha20-Poly1305 (for connect tokens) | P2-A.1 | 2h |
| P2-A.4 | CRC32C (generic + SIMD) | P1-A.3, P1-A.4 | 4h |
| P2-A.5 | CSPRNG wrapper | P2-A.1 | 1h |
| P2-B.1 | Connect token structure (2048 bytes) | P0.5 | 3h |
| P2-B.2 | `netudp_generate_connect_token()` | P2-A.3, P2-B.1 | 4h |
| P2-B.3 | Token validation (server-side decrypt) | P2-A.3, P2-B.1 | 4h |
| P2-B.4 | Token HMAC tracking (prevent reuse) | P2-B.3, P1-C.4 | 2h |
| P2-C.1 | Replay protection (256-entry uint64 window) | P0.7 | 3h |
| P2-D.1 | Packet encrypt (ChaCha20, seq as nonce) | P2-A.2, P2-C.1 | 4h |
| P2-D.2 | Packet decrypt + replay check | P2-D.1 | 3h |
| P2-D.3 | AAD construction (version + proto_id + prefix) | P2-D.1 | 1h |
| P2-E.1 | Per-IP token bucket rate limiter | P1-C.4 | 3h |
| P2-E.2 | Client state machine (10 states) | P0.7 | 4h |
| P2-E.3 | Handshake: CONNECTION_REQUEST handling | P2-B.3, P2-E.1 | 4h |
| P2-E.4 | Handshake: CHALLENGE generation + validation | P2-D.1, P2-E.3 | 4h |
| P2-E.5 | Handshake: RESPONSE + connection establishment | P2-E.4 | 4h |
| P2-E.6 | Multi-server fallback (client) | P2-E.2, P2-E.5 | 3h |
| P2-E.7 | Integration test: connect + encrypted echo | P2-E.5 | 3h |

### Phase 3 — Reliability + Channels (P3)
| ID | Task | Dependencies | Est. |
|---|---|---|---|
| P3-A.1 | Packet sequence numbering (uint16) | P2-E.5 | 2h |
| P3-A.2 | Piggybacked ack + ack_bits (uint32) | P3-A.1 | 4h |
| P3-A.3 | Ack delay field (uint16 microseconds) | P3-A.2 | 2h |
| P3-A.4 | Sequence window overflow protection | P3-A.2 | 2h |
| P3-B.1 | RTT estimation (SRTT, RTTVAR, RTO) | P3-A.3 | 3h |
| P3-C.1 | Channel type definitions (4 types) | P0.5 | 2h |
| P3-C.2 | Channel config (priority, weight, nagle) | P3-C.1 | 3h |
| P3-C.3 | Nagle timer + NoNagle + flush | P3-C.2 | 4h |
| P3-C.4 | Priority + weight scheduling | P3-C.2 | 4h |
| P3-D.1 | Per-channel reliable sequencing | P3-A.2, P3-C.1 | 4h |
| P3-D.2 | Reliable send buffer (FixedRingBuffer<SentMessage>) | P3-D.1, P1-C.3 | 4h |
| P3-D.3 | Reliable retransmission (RTT-adaptive, exp backoff) | P3-D.2, P3-B.1 | 6h |
| P3-D.4 | Reliable ordered delivery (reorder buffer) | P3-D.1 | 4h |
| P3-D.5 | Reliable unordered delivery | P3-D.1 | 2h |
| P3-D.6 | Unreliable sequenced (drop stale) | P3-A.1, P3-C.1 | 2h |
| P3-D.7 | Stop-waiting optimization | P3-A.2 | 3h |
| P3-D.8 | Keepalive (empty packet with acks) | P3-A.2 | 2h |
| P3-D.9 | Integration test: reliable + unreliable mix | P3-D.4, P3-D.6 | 4h |

### Phase 4 — Fragmentation + Multi-Frame (P4)
| ID | Task | Dependencies | Est. |
|---|---|---|---|
| P4-A.1 | Fragment header (message_id, index, count) | P3-D.1 | 2h |
| P4-A.2 | Message splitting at MTU boundary | P4-A.1 | 4h |
| P4-A.3 | Fragment bitmask tracking (SIMD popcount) | P4-A.1, P1-A.4 | 4h |
| P4-A.4 | Reassembly buffer (pre-allocated, timeout) | P4-A.3, P1-C.2 | 4h |
| P4-A.5 | Fragment-level retransmission | P4-A.3, P3-D.3 | 4h |
| P4-B.1 | Multi-frame packet assembly (ack + data frames) | P3-A.2, P3-C.3 | 6h |
| P4-B.2 | Frame type encoding/decoding | P4-B.1 | 3h |
| P4-B.3 | Variable-length sequence in prefix byte | P4-B.1 | 2h |
| P4-C.1 | Full send pipeline integration | P4-A.2, P4-B.1, P2-D.1 | 6h |
| P4-C.2 | Full recv pipeline integration | P4-A.4, P4-B.2, P2-D.2 | 6h |
| P4-C.3 | Integration test: large message (64KB+) | P4-C.1, P4-C.2 | 3h |

### Phase 5 — Compression + Stats + DDoS (P5)
| ID | Task | Dependencies | Est. |
|---|---|---|---|
| P5-A.1 | Per-connection token bucket | P4-C.1 | 3h |
| P5-A.2 | Per-connection QueuedBits pattern | P5-A.1 | 3h |
| P5-A.3 | AIMD congestion control | P5-A.2, P3-B.1 | 6h |
| P5-B.1 | netc library integration (CMake FetchContent) | P4-C.1 | 3h |
| P5-B.2 | Stateful compression (reliable ordered) | P5-B.1 | 4h |
| P5-B.3 | Stateless compression (unreliable) | P5-B.1 | 3h |
| P5-B.4 | Dictionary loading from config | P5-B.1 | 2h |
| P5-C.1 | DDoS severity state machine (5 levels) | P2-E.1 | 4h |
| P5-C.2 | Bad packet counters + auto-escalation | P5-C.1 | 3h |
| P5-C.3 | Auto-cooloff | P5-C.2 | 2h |
| P5-D.1 | Per-connection stats struct | P3-B.1, P5-A.2 | 3h |
| P5-D.2 | Per-channel stats | P5-D.1 | 2h |
| P5-D.3 | Global server stats | P5-D.1 | 2h |
| P5-D.4 | Compression ratio tracking | P5-B.2, P5-D.1 | 1h |

### Phase 6 — Polish + Benchmarks (P6)
| ID | Task | Dependencies | Est. |
|---|---|---|---|
| P6-A.1 | Network simulator (latency, loss, dup, reorder) | P4-C.1 | 6h |
| P6-B.1 | Packet handler registration API | P4-C.2 | 3h |
| P6-B.2 | Connection lifecycle callbacks | P2-E.5 | 2h |
| P6-B.3 | Buffer acquire/send (zero-copy) | P1-C.2 | 3h |
| P6-B.4 | Buffer read/write helpers | P6-B.3 | 4h |
| P6-B.5 | Broadcast / multicast helpers | P6-B.1 | 2h |
| P6-C.1 | Automatic rekeying (1GB / 1h) | P2-D.1 | 4h |
| P6-D.1 | Benchmark framework (runner + reporter) | P0.1 | 4h |
| P6-D.2 | bench_pps (packets per second) | P6-D.1, P4-C.1 | 4h |
| P6-D.3 | bench_latency (histogram) | P6-D.1, P4-C.1 | 4h |
| P6-D.4 | bench_simd_compare | P6-D.1, P1-A.4 | 3h |
| P6-D.5 | bench_scalability (PPS vs connections) | P6-D.2 | 3h |
| P6-D.6 | CI benchmark regression (fail on >5%) | P6-D.2 | 3h |

### Phase 7 — Optimization + v1.0 (P7)
| ID | Task | Dependencies | Est. |
|---|---|---|---|
| P7-A.1 | recvmmsg/sendmmsg (Linux) | P1-B.3, P4-C.1 | 6h |
| P7-B.1 | Batch send API | P4-C.1 | 3h |
| P7-B.2 | Batch receive API | P4-C.2 | 3h |
| P7-C.1 | Echo server example | P6-B.1 | 3h |
| P7-C.2 | Chat example | P7-C.1 | 4h |
| P7-C.3 | Stress test example | P6-D.2 | 4h |
| P7-D.1 | v1.0 release checklist + tag | ALL P7 | 4h |

### Phase 8 — SDK Bindings (P8)
| ID | Task | Dependencies | Est. |
|---|---|---|---|
| P8-A.1 | C++ RAII wrapper (`netudp.hpp`) | P7-D.1 | 6h |
| P8-B.1 | Unreal 5 plugin (.uplugin + subsystem) | P8-A.1 | 16h |
| P8-C.1 | Unity C# bindings (P/Invoke + NativeArray) | P7-D.1 | 12h |
| P8-D.1 | Godot 4 GDExtension | P7-D.1 | 12h |
| P8-E.1 | UzEngine NetworkingSubsystem | P8-A.1 | 8h |

---

## Critical Path

```
P0 → P1-B (sockets) → P1-E (send/recv) → P2-E (handshake) → P3-D (reliability)
→ P4-C (full pipeline) → P6-D (benchmarks) → P7-D (v1.0)
```

**Estimated critical path duration: ~180 hours of focused implementation.**
