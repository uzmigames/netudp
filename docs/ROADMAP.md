# netudp — Roadmap

## Release Timeline

```
v0.1.0 ─── Foundation + Encrypted Connections ─── Target: Week 4
v0.2.0 ─── Reliability + Channels ──────────── Target: Week 8
v0.3.0 ─── Fragmentation + Full Pipeline ───── Target: Week 10
v0.4.0 ─── Compression + Stats + DDoS ──────── Target: Week 12
v0.5.0 ─── Benchmarks + Network Simulator ──── Target: Week 14
v1.0.0 ─── Production Ready ────────────────── Target: Week 16
v1.1.0 ─── SDK: C++ Wrapper + UzEngine ─────── Target: Week 18
v1.2.0 ─── SDK: Unreal + Unity + Godot ─────── Target: Week 22
```

---

## v0.1.0 — Foundation + Encrypted Connections

**Goal:** Client connects to server with encrypted connect token. Bidirectional encrypted data exchange (no reliability, no channels).

### Deliverables
- [ ] CMake + Zig CC build system, CI (Win/Linux/Mac)
- [ ] Google Test harness, clang-tidy
- [ ] Public header structure (`include/netudp/`)
- [ ] SIMD runtime detection + dispatch table
- [ ] Platform socket abstraction (Win Winsock2, Linux/Mac BSD)
- [ ] Buffer pool (`Pool<T>`) + custom allocator
- [ ] Fixed containers (`FixedRingBuffer`, `FixedHashMap`)
- [ ] Address parsing + comparison (IPv4/IPv6)
- [ ] Vendored ChaCha20-Poly1305 (monocypher)
- [ ] CRC32C (generic + SSE4.2 + NEON)
- [ ] Connect token generation + validation
- [ ] 4-step handshake (request → challenge → response → connected)
- [ ] AEAD packet encryption (seq as nonce, separate Tx/Rx keys)
- [ ] Replay protection (256-entry window)
- [ ] Client state machine (10 states, multi-server fallback)
- [ ] Per-IP token bucket rate limiting
- [ ] Basic send/recv (encrypted, unreliable only)
- [ ] Test: echo server with encryption

### Exit Criteria
- Client connects with connect token, exchanges encrypted packets
- All tests pass on Windows + Linux + macOS
- Zero-GC verified: no allocations after `server_start()`

---

## v0.2.0 — Reliability + Channels

**Goal:** 4 channel types working. Reliable messages delivered in order. RTT measured from ack delay.

### Deliverables
- [ ] Packet sequence numbers (uint16) + piggybacked ack + ack_bits
- [ ] Ack delay field for RTT estimation (no separate ping)
- [ ] SRTT / RTTVAR / RTO calculation
- [ ] 4 channel types: unreliable, unreliable_sequenced, reliable_ordered, reliable_unordered
- [ ] Per-channel independent reliable sequencing (512 outstanding)
- [ ] Reliable retransmission (RTT-adaptive, exponential backoff, max 10)
- [ ] Reliable ordered reorder buffer
- [ ] Unreliable sequenced (drop stale)
- [ ] Channel priority + weight scheduling
- [ ] Nagle timer (per-channel, configurable, NoNagle per-message)
- [ ] Stop-waiting optimization
- [ ] Keepalive packets (empty with ack header)
- [ ] Sequence window overflow protection
- [ ] Test: reliable message delivery under 10% packet loss

### Exit Criteria
- All 4 channel types working correctly
- Reliable messages delivered in order under 20% loss
- RTT estimation within 10% of actual

---

## v0.3.0 — Fragmentation + Full Pipeline

**Goal:** Large messages (up to 512KB) work transparently. Multi-frame packets carry ack + data together.

### Deliverables
- [ ] Fragment header (message_id, index, count)
- [ ] Message splitting at MTU boundary
- [ ] Fragment bitmask tracking (SIMD popcount for missing fragments)
- [ ] Reassembly buffer (pre-allocated, timeout cleanup)
- [ ] Fragment-level retransmission (only lost fragments, not whole message)
- [ ] Multi-frame packet assembly (ack + stop-waiting + data frames)
- [ ] Frame type encoding/decoding
- [ ] Variable-length sequence in prefix byte
- [ ] Full send pipeline: app → channel → compress → fragment → encrypt → batch → socket
- [ ] Full recv pipeline: socket → decrypt → defragment → decompress → channel → app
- [ ] Test: 64KB + 256KB + 512KB message delivery under loss

### Exit Criteria
- 512KB messages reassembled correctly under 10% loss
- Only lost fragments retransmitted (not whole message)
- Multi-frame packets verified (ack + data in same UDP packet)

---

## v0.4.0 — Compression + Stats + DDoS

**Goal:** netc compression integrated. Comprehensive statistics. DDoS protection.

### Deliverables
- [ ] netc library integration (CMake FetchContent or vendored)
- [ ] Stateful compression for reliable ordered channels
- [ ] Stateless compression for unreliable channels
- [ ] Dictionary loading from config
- [ ] Per-connection bandwidth: token bucket + QueuedBits
- [ ] AIMD congestion control
- [ ] DDoS severity escalation (5 levels, auto-cooloff)
- [ ] Per-connection stats (ping, quality, throughput, queue, compression)
- [ ] Per-channel stats
- [ ] Global server stats
- [ ] Automatic rekeying (1GB / 1 hour)
- [ ] Test: compression ratio matches netc benchmarks
- [ ] Test: DDoS escalation under flood

### Exit Criteria
- Compression working on all channel types
- Stats match expected values under controlled conditions
- DDoS protection blocks flood without affecting legitimate connections

---

## v0.5.0 — Benchmarks + Network Simulator

**Goal:** Benchmark suite proves performance targets. Network simulator enables testing adverse conditions.

### Deliverables
- [ ] Network simulator (latency, jitter, loss, dup, reorder, incoming lag)
- [ ] Packet handler registration API
- [ ] Connection lifecycle callbacks (on_connect, on_disconnect, on_tick)
- [ ] Buffer acquire/send (zero-copy pattern)
- [ ] Buffer read/write helpers (u8-u64, f32, varint, string)
- [ ] Broadcast / multicast helpers
- [ ] Benchmark: PPS (target ≥ 2M PPS)
- [ ] Benchmark: latency histogram (target p99 ≤ 5µs)
- [ ] Benchmark: SIMD comparison (generic vs SSE vs AVX vs NEON)
- [ ] Benchmark: scalability (PPS vs connection count)
- [ ] Benchmark: memory usage
- [ ] CI benchmark regression (fail on >5%)

### Exit Criteria
- PPS ≥ 2M on desktop hardware (64B, encrypted, reliable)
- p99 ≤ 5µs loopback latency
- All SIMD paths show ≥ 20% improvement over scalar
- CI benchmarks running and preventing regression

---

## v1.0.0 — Production Ready

**Goal:** Stable, documented, benchmarked, ready for production game servers.

### Deliverables
- [ ] recvmmsg/sendmmsg batch I/O (Linux)
- [ ] Batch send/receive API
- [ ] Echo server example
- [ ] Chat server example
- [ ] Stress test example (1000 connections)
- [ ] Cache-line alignment optimization
- [ ] Full API documentation (Doxygen or similar)
- [ ] Migration guide from netcode.io
- [ ] CHANGELOG.md
- [ ] v1.0.0 git tag

### Exit Criteria
- All FR and NFR from PRD met
- ≥ 90% test coverage
- Zero known critical bugs
- Benchmarks meet all targets
- Documentation complete

---

## v1.1.0 — SDK: C++ Wrapper + UzEngine

**Goal:** Native C++ integration for UzEngine and Unreal-ready header.

### Deliverables
- [ ] C++ header-only RAII wrapper (`netudp.hpp`)
- [ ] `netudp::Server`, `netudp::Client`, `netudp::Message` classes
- [ ] Move semantics, `std::span`, RAII release
- [ ] UzEngine `NetworkingSubsystem` (ISubsystem, EventQueue, PoolAllocator)
- [ ] UzEngine integration tests

### Exit Criteria
- UzEngine echo server running with NetworkingSubsystem
- C++ wrapper compiles without C API includes

---

## v1.2.0 — SDK: Unreal + Unity + Godot

**Goal:** Engine plugins/bindings for the three major engines.

### Deliverables
- [ ] Unreal Engine 5 plugin (.uplugin + UNetudpSubsystem)
- [ ] Unity package (C# P/Invoke + NativeArray)
- [ ] Godot 4 GDExtension (C binding + GDScript API)
- [ ] Cross-compiled platform libraries for all targets
- [ ] Per-engine quick-start guide
- [ ] Per-engine example project

### Exit Criteria
- Echo server running in each engine
- Platform matrix: Win/Linux/Mac × x64/ARM64

---

## Future (Post-v1.2)

| Version | Feature | Notes |
|---|---|---|
| v1.3 | Android + iOS builds | Mobile platform support |
| v1.4 | AES-256-GCM compile option | For AES-NI platforms |
| v1.5 | WebRTC data channel fallback | Browser client support |
| v2.0 | Replication framework | Property replication layer on top of netudp (UzEngine-specific) |
