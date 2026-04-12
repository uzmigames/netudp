# netudp — Product Requirements Document (PRD)

**Version:** 1.0  
**Date:** 2026-04-11  
**Author:** Andre Ferreira (@andrehrferreira)  
**Status:** Draft

---

## 1. Product Overview

**netudp** is a zero-GC, SIMD-accelerated, C++17 UDP networking library for high-performance multiplayer game servers. It provides encrypted, reliable and unreliable message delivery over UDP with built-in compression, congestion control, and comprehensive diagnostics.

**One-line:** "What netcode.io + reliable.io + Oodle would be if they were a single zero-GC library with modern crypto and SIMD."

## 2. Problem Statement

Game developers building multiplayer servers face three choices:
1. **netcode.io** — connection + encryption only. No reliability, no channels, no compression. Must pair with reliable.io manually.
2. **Valve GNS** — feature-complete but 30K+ lines of C++, requires OpenSSL, Steam-coupled naming, complex build.
3. **Unreal NetDriver** — battle-tested but UObject/GC coupled, proprietary Oodle compression, 8-channel limit, no standalone library.

None offers a single, zero-GC, SIMD-accelerated, easy-to-integrate library with modern crypto AND compression AND reliability AND engine SDK bindings.

## 3. Target Users

| User | Need |
|---|---|
| **UzmiGames Engine team** | Native C++ integration for custom engine multiplayer |
| **Indie game developers** | Drop-in networking lib, easier than rolling their own UDP |
| **Unreal Engine developers** | Alternative to built-in networking with better compression (netc > Oodle) |
| **Unity developers** | Native plugin with C# bindings, zero-GC (no managed allocation spikes) |
| **Godot developers** | GDExtension for high-performance dedicated servers |
| **Custom engine developers** | Clean C API, vendored deps, trivial cross-compile |

## 4. Success Metrics

| Metric | Target | Measurement |
|---|---|---|
| Packets per second (64B, encrypted) | ≥ 2M PPS single core | Benchmark: `bench_pps` |
| End-to-end latency (loopback) | p99 ≤ 5 µs | Benchmark: `bench_latency` |
| Memory (1024 connections, no compression) | ≤ 100 MB | Benchmark: `bench_memory` |
| Zero-GC compliance | 0 allocations after init | Static analysis + runtime assert |
| Compression ratio (64B game packets) | ≤ 0.76 (netc) | Benchmark: `bench_compression` |
| Build time (clean, Release) | ≤ 30 seconds | CI measurement |
| Integration time (new engine) | ≤ 1 day to echo server | User testing |
| Test coverage | ≥ 90% line coverage | gcov/llvm-cov |
| Platforms | Windows, Linux, macOS, Android, iOS | CI matrix |

## 5. Functional Requirements

### FR-1: Connection Management
- FR-1.1: Connect token generation and validation (netcode.io protocol)
- FR-1.2: 4-step handshake (request → challenge → response → connected)
- FR-1.3: Client state machine with 10 states (6 error + 4 normal)
- FR-1.4: Multi-server fallback (up to 32 servers per token)
- FR-1.5: Graceful disconnect with redundant packets
- FR-1.6: Connection timeout (configurable, default 10s)
- FR-1.7: Max connections configurable (default 256, up to 65535)
- FR-1.8: Connection slot reuse with generation counter

### FR-2: Channels
- FR-2.1: 4 channel types: unreliable, unreliable sequenced, reliable ordered, reliable unordered
- FR-2.2: Up to 255 channels per connection
- FR-2.3: Per-channel priority + weight scheduling
- FR-2.4: Per-channel Nagle timer (configurable, bypass per-message)
- FR-2.5: Per-channel compression mode (stateful/stateless via netc)
- FR-2.6: Per-channel statistics

### FR-3: Reliability
- FR-3.1: Dual-layer: packet-level ack + per-channel message reliability
- FR-3.2: 16-bit packet sequence + 32-bit ack bitmask + ack delay
- FR-3.3: Per-channel independent reliable sequencing (up to 512 outstanding)
- FR-3.4: RTT estimation from ack delay (SRTT, no separate ping/pong)
- FR-3.5: RTT-adaptive retransmission with exponential backoff
- FR-3.6: Stop-waiting optimization
- FR-3.7: Max 10 retries per message, then drop with quality tracking

### FR-4: Fragmentation
- FR-4.1: Transparent split/reassemble for messages > MTU
- FR-4.2: Fragment-level bitmask tracking (retransmit only lost fragments)
- FR-4.3: Max message size configurable (default 64KB, up to 288KB — limited by 255 fragments × MTU payload)
- FR-4.4: Fragment timeout with cleanup (default 5s)

### FR-5: Encryption
- FR-5.1: ChaCha20-Poly1305 AEAD default (vendored, no external deps)
- FR-5.2: AES-256-GCM compile-time option (for AES-NI platforms)
- FR-5.3: Separate Tx/Rx keys from connect token
- FR-5.4: Sequence number as nonce (deterministic)
- FR-5.5: 256-entry replay protection window
- FR-5.6: Automatic rekeying at 1GB / 1 hour
- FR-5.7: CRC32C-only mode for LAN/dev (compile flag)

### FR-6: Compression
- FR-6.1: netc integration (optional, via dictionary)
- FR-6.2: Stateful compression for reliable ordered channels
- FR-6.3: Stateless compression for unreliable channels
- FR-6.4: Passthrough guarantee (never expands payload)
- FR-6.5: Compress before encrypt

### FR-7: Bandwidth Control
- FR-7.1: Per-connection token bucket rate limiting
- FR-7.2: Per-IP rate limiting (pre-connection, DDoS layer 1)
- FR-7.3: AIMD congestion avoidance
- FR-7.4: Send rate estimation exposed in stats
- FR-7.5: DDoS escalation (5 severity levels with auto-cooloff)

### FR-8: Wire Format
- FR-8.1: Multi-frame packets (ack + stop-waiting + data in one UDP packet)
- FR-8.2: Variable-length sequence encoding (1-8 bytes)
- FR-8.3: Header authenticated as AEAD associated data
- FR-8.4: Max payload 1200 bytes, max packet 1400 bytes on wire

### FR-9: Statistics
- FR-9.1: Per-connection: ping, quality, throughput, queue depth, compression ratio
- FR-9.2: Per-channel: pending bytes, unacked bytes, queue time
- FR-9.3: Global server: total connections, PPS, bandwidth

### FR-10: API
- FR-10.1: `extern "C"` public API for universal FFI
- FR-10.2: C++ RAII wrapper (header-only)
- FR-10.3: Opaque handles (not raw pointers)
- FR-10.4: Custom allocator support
- FR-10.5: Explicit time parameter (no internal clock)
- FR-10.6: Packet handler registration (callback per type)
- FR-10.7: Connection lifecycle callbacks
- FR-10.8: Zero-copy buffer acquire/send
- FR-10.9: Broadcast / multicast helpers
- FR-10.10: Batch send / batch receive

### FR-11: Platform
- FR-11.1: Windows (x64, ARM64)
- FR-11.2: Linux (x64, ARM64)
- FR-11.3: macOS (x64, ARM64)
- FR-11.4: Android (ARM64) — future
- FR-11.5: iOS (ARM64) — future

### FR-12: SDK Bindings
- FR-12.1: C++ header-only wrapper (RAII, span, move semantics)
- FR-12.2: UzmiGames Engine subsystem (ISubsystem, EventQueue, PoolAllocator)
- FR-12.3: Unreal Engine 5 plugin
- FR-12.4: Unity C# P/Invoke bindings
- FR-12.5: Godot 4 GDExtension

## 6. Non-Functional Requirements

### NFR-1: Performance
- Zero heap allocation after initialization
- SIMD acceleration for all bulk data operations (SSE4.2, AVX2, NEON)
- ≥ 2M PPS on modern desktop CPU
- p99 latency ≤ 5 µs per packet (loopback)

### NFR-2: Security
- All traffic encrypted by default (no opt-out in production builds)
- Replay protection on all encrypted packets
- Stateless handshake (no server state before token validation)
- DDoS escalation with auto-cooloff
- No plaintext credentials on wire

### NFR-3: Reliability
- Library never crashes on malformed input (fuzz tested)
- Bounds-checked decompressor
- CRC32 dictionary validation
- Graceful degradation under packet loss (up to 30%)

### NFR-4: Portability
- C++17 standard (no compiler-specific extensions in public API)
- Builds with GCC, Clang, MSVC
- Cross-compilation via Zig CC
- No external dependencies (vendored crypto)

### NFR-5: Maintainability
- ≤ 15,000 lines of implementation code
- ≥ 90% test coverage
- CI on every commit (Windows + Linux + macOS)
- Benchmark regression tests

## 7. Out of Scope

- Game-specific logic (entities, replication, RPCs, AI)
- Entity serialization / delta encoding (application-level)
- Matchmaking / lobby / session management
- Voice chat / media streaming
- P2P / NAT traversal / STUN / ICE
- WebSocket / WebRTC fallback
- HTTP / REST / GraphQL
- Database integration
- Account management / authentication (backend provides connect tokens)
