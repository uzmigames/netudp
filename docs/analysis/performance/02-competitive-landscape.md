# 02. Competitive Landscape

Published benchmarks for game server networking libraries. All numbers are from public sources (see [09-sources.md](09-sources.md)).

## Game Networking Libraries

### ENet

Thin reliable-UDP library in C, widely used since early 2000s.

| Metric | Value | Platform | Source |
|--------|-------|----------|--------|
| PPS (C# wrapper) | ~184,000 msg/s | Ubuntu, i5-3570K, 32B | NetworkBenchmarkDotNet v0.8.2 |
| PPS (C# wrapper) | ~103,000 msg/s | Win11, Ryzen 7 3700X, 32B | NetworkBenchmarkDotNet v0.8.2 |
| PPS (raw C, estimated) | ~250,000–400,000 | Linux | Estimated 2–4× C# wrapper |
| Packet overhead | 10 bytes | — | ENet-CSharp docs |
| Crypto | None built-in | — | — |

Caveats:
- Default `ENET_PEER_DEFAULT_ROUND_TRIP_TIME = 500ms` → 1–2s perceived lag under loss until changed to ~50ms
- No multi-thread send path — single `enet_host_service()` loop
- C# wrapper reduces raw C performance; no published C-only PPS benchmark

### GameNetworkingSockets (Valve/Steam)

Production networking library for Steam multiplayer. AES-256-GCM + Curve25519.

| Metric | Value | Notes | Source |
|--------|-------|-------|--------|
| Default throughput | ~1 MB/s | Localhost, default SendRateMax | GNS Issue #198 |
| Tuned throughput | ~8.5–10 MB/s | Localhost, manual rate cap removal | GNS Issue #198 |
| Throughput collapse | ~1 MB/s | When send buffer > 268 MB pending | GNS Issue #198 |
| Crypto | AES-256-GCM per-packet | Curve25519 key exchange | — |
| Multi-thread (pre-v1.3) | Very poor | Severe lock contention | GNS v1.3 release |
| Multi-thread (v1.3+) | Improved | Fine-grained locking | GNS v1.3 release |

Caveats:
- SendRateMax must be explicitly set to a large value or throughput is severely capped
- ~8.5 MB/s ÷ 1200 bytes ≈ 7,083 pps — **extremely low** for a C++ library
- No official Valve PPS benchmark published

### yojimbo / reliable.io / netcode.io (Glenn Fiedler)

Suite of C libraries for client/server games. Auth + reliability + connection management.

| Metric | Value | Notes | Source |
|--------|-------|-------|--------|
| Published PPS | None | No public benchmark data | — |
| Design send rate | 60 packets/s per client | Example from Gaffer On Games | Blog |
| Target client count | 2–64 typical, 256 max | STANDARD.md | netcode GitHub |
| Crypto | AES-256-GCM (libsodium) | Connect tokens: XSalsa20+Poly1305 | — |
| Protocol overhead | ~8 bytes header | Custom reliable layer | — |

Caveats:
- Designed for low-player-count, low-frequency games (FPS: 60pps × 64 = 3,840 total pps)
- netcode.io is auth-only — not a transport library
- No PPS or throughput benchmarks published anywhere

### LiteNetLib

Pure C# reliable-UDP library for .NET/Mono/Unity.

| Metric | Value | Platform | Source |
|--------|-------|----------|--------|
| PPS | ~93,000 msg/s | Ubuntu, i5-3570K, 32B | NetworkBenchmarkDotNet v0.8.2 |
| PPS | ~101,000 msg/s | Win11, Ryzen 7 3700X, 32B | NetworkBenchmarkDotNet v0.8.2 |
| Packet overhead (unreliable) | 1 byte | Smallest of all .NET libs | — |
| Packet overhead (reliable) | 4 bytes | — | — |
| Crypto | None built-in | — | — |

### KCP Protocol

ARQ protocol trading 10–20% bandwidth for 30–40% lower latency.

| Metric | Value | Platform | Source |
|--------|-------|----------|--------|
| PPS (Kcp2k C# wrapper) | ~24,000 msg/s | Ubuntu, i5-3570K, 32B | NetworkBenchmarkDotNet v0.8.2 |
| PPS (Kcp2k C# wrapper) | ~15,000 msg/s | Win11, Ryzen 7 3700X, 32B | NetworkBenchmarkDotNet v0.8.2 |
| Throughput (kcp-go, 4KB) | ~124 MB/s echo | Linux, Ryzen 9 5950X | kcp-go README |
| Latency vs ENet (under loss) | ~269ms KCP vs ~1,470ms ENet | ASIO-KCP benchmark | reliable_udp_bench_mark |
| Packet overhead | 24 bytes | Highest of all tested | — |

Caveats:
- C# Kcp2k wrapper is 7–10× slower than ENet — raw C implementation is faster but no direct comparison exists
- KCP wins on **latency under loss**, not raw PPS
- Used in production: Genshin Impact (HoYoverse), SpatialOS

### .NET 8 System.Net.Sockets UDP

.NET's built-in UDP socket layer.

| Metric | Value | Platform | Source |
|--------|-------|----------|--------|
| PPS send (1,380B) | ~67,000 | GbE, i7-9750H | Enclave |
| PPS recv (1,380B) | ~78,000 | Same | Enclave |
| PPS recv (zero-alloc) | ~81,000 | .NET 8, SocketAddress reuse | Enclave |
| Throughput (65,535B) | ~912 Mbps @ 1,829 pps | 1 GbE limited | Enclave |

## Game Engine Networking

### Unreal Engine NetDriver (UDP)

High-level replication system, not a raw transport library.

| Metric | Value | Notes | Source |
|--------|-------|-------|--------|
| Typical tick rate | 30–60 Hz | `NetServerMaxTickRate` config | UE docs |
| PPS per client (idle) | 6–8 pps | Baseline network activity | UT2004 paper |
| PPS total (UT2004 match) | ~83 pps | Full active match | UT2004 paper |
| Bandwidth per extra client | ~20 Kbps | Incremental | Research measurement |
| MTU target | 1,200 bytes | Conservative (same as netudp) | UE docs |
| Reliable buffer | 256 unacked packets | Before disconnect | RELIABLE_BUFFER const |
| Crypto | None built-in | DTLS optional via EOS | — |

Caveats:
- UE networking is a **replication system**, not a PPS benchmark target
- Each packet carries hundreds of property deltas, RPCs, and actor state — raw PPS is irrelevant to its architecture
- UE5 Iris replication improved bandwidth efficiency but no public benchmarks

### Unity Netcode / UTP (Unity Transport Protocol)

Unity's built-in transport for Netcode for GameObjects.

| Metric | Value | Notes |
|--------|-------|-------|
| Published PPS | None | Unity has published zero benchmark numbers |
| Max message size | 1,400 bytes | `maxMessageSize` default |
| Crypto | Optional DTLS via Unity Relay | Not in self-hosted mode |
| Reliability | Head-of-line blocking on reliable pipeline | Known limitation |

Caveats:
- No published PPS or throughput benchmarks from Unity
- UTP 2.0 added fragmentation, batched sends, custom pipelines
- Raw socket layer falls into .NET 6/7/8 numbers

## Linux Kernel Socket Ceiling

Reference numbers for the maximum achievable on standard Linux.

| Configuration | PPS | Hardware | Source |
|---------------|-----|----------|--------|
| Single thread, no pinning | 197,000–350,000 | Xeon E5, 10 GbE | Cloudflare |
| Single thread, CPU-pinned, NUMA-local | 360,000–430,000 | Same | Cloudflare |
| 4 threads + SO_REUSEPORT | 1,100,000–1,147,000 | Same | Cloudflare |
| io_uring | ~7,000,000 | Modern server | Kernel bypass research |
| DPDK (1 core, MoonGen) | ~10,000,000–14,880,000 | 10 GbE line rate | ServerAsCode |

## Consolidated Comparison Table

| Library | Lang | PPS (1 thread) | Platform | Pkt Size | Crypto | Batch I/O |
|---------|------|---------------:|----------|----------|--------|-----------|
| ENet (C# wrap) | C/C# | 184,000 | Linux | 32 B | No | No |
| ENet (C# wrap) | C/C# | 103,000 | Windows | 32 B | No | No |
| **netudp** | **C++** | **138,000** | **Windows** | **1,200 B** | **XChaCha20** | **Yes** |
| LiteNetLib | C# | 101,000 | Windows | 32 B | No | No |
| .NET 8 raw | C# | 81,000 | Windows | 1,380 B | No | No |
| Kcp2k | C# | 24,000 | Linux | 32 B | No | No |
| GNS (Valve) | C++ | ~7,000 | Localhost | Mixed | AES-GCM | No |
| Unreal | C++ | ~83 | Windows | ~1,200 B | No | No |
