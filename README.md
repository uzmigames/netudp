# netudp — UDP Networking Library

[![Language](https://img.shields.io/badge/language-C%2B%2B17-orange.svg)](https://en.cppreference.com/w/cpp/17)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()
[![Version](https://img.shields.io/badge/version-0.1.0--dev-blue.svg)](CHANGELOG.md)

> High-performance, zero-GC UDP networking for real-time game servers. Encrypted, reliable, SIMD-accelerated. Built for dedicated servers handling thousands of concurrent players at 100K+ packets/second.

---

## Key Features

- **Zero-GC** — no allocations after init. All memory pre-allocated from pools. Runs indefinitely without touching the heap
- **SIMD-accelerated** — runtime dispatch to SSE4.2 / AVX2 / NEON for crypto, CRC32C, buffer ops, ack scanning, fragment tracking, address comparison
- **Connect tokens** — secure authentication via encrypted tokens (netcode.io protocol). No online key exchange needed
- **4 channel types** — unreliable, unreliable sequenced, reliable ordered, reliable unordered. Priority + weight scheduling
- **Dual-layer reliability** — piggybacked acks with delay field for continuous RTT. Per-channel message sequencing with RTT-adaptive retransmission
- **Fragmentation** — transparent split/reassemble for messages up to 288KB. Fragment-level retransmission (only lost fragments, not whole message)
- **Encryption** — ChaCha20-Poly1305 AEAD by default. AES-256-GCM compile-time option. 256-entry replay window. Auto-rekeying at 1GB/1h
- **Compression** — optional [netc](https://github.com/uzmigames/netc) integration (35-67% bandwidth savings on game packets). Stateful for reliable, stateless for unreliable
- **Multi-frame packets** — ack + stop-waiting + data frames in one UDP packet. Nagle batching with per-message bypass
- **Bandwidth control** — per-connection token bucket + QueuedBits budget + AIMD congestion avoidance
- **GNS-level stats** — ping, quality, throughput, queue depth, compression ratio, packet loss, fragment tracking per connection/channel
- **DDoS protection** — 5-severity escalation (None → Low → Medium → High → Critical) with auto-cooloff
- **Packet interfaces** — handler registration, lifecycle callbacks, zero-copy buffer acquire/send
- **Cross-platform** — Windows, Linux, macOS via CMake + Zig CC. Android, iOS planned
- **Engine SDKs** — C++ header-only wrapper, Unreal 5 plugin, Unity C# bindings, Godot 4 GDExtension
- **Clean `extern "C"` API** — POD structs, opaque handles, zero dependencies beyond libc. Bindable from any language

---

## Quick Start

### Build (Linux / macOS)

```bash
git clone https://github.com/uzmigames/netudp.git
cd netudp

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

ctest --test-dir build --output-on-failure
```

### Build (Windows / MSVC)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

ctest --test-dir build -C Release --output-on-failure
```

### Cross-Compile (Zig CC)

```bash
# Linux target from any host
cmake -B build-linux -DCMAKE_C_COMPILER="zig cc" -DCMAKE_CXX_COMPILER="zig c++" -DCMAKE_SYSTEM_NAME=Linux
cmake --build build-linux
```

### Run Benchmarks

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DNETUDP_BUILD_BENCH=ON
cmake --build build --config Release

./build/bench/bench_pps          # Packets per second
./build/bench/bench_latency      # Latency histogram
./build/bench/bench_simd_compare # SIMD vs scalar comparison
```

---

## Usage

### Server

```c
#include <netudp/netudp.h>

void on_connect(void* ctx, int client, uint64_t id, const uint8_t user_data[256]) {
    printf("Client %d connected (id=%llu)\n", client, id);
}

void on_disconnect(void* ctx, int client, int reason) {
    printf("Client %d disconnected (reason=%d)\n", client, reason);
}

int main() {
    netudp_init();

    netudp_server_config_t config = {0};
    config.protocol_id = 0x1234567890ABCDEF;
    config.num_channels = 4;
    config.on_connect = on_connect;
    config.on_disconnect = on_disconnect;
    // config.private_key = ... (32 bytes, shared with backend)

    netudp_server_t* server = netudp_server_create("0.0.0.0:27015", &config, 0.0);
    netudp_server_start(server, 1024);

    while (running) {
        double time = get_time();
        netudp_server_update(server, time);

        for (int c = 0; c < 1024; c++) {
            netudp_message_t* msgs[64];
            int count = netudp_server_receive(server, c, msgs, 64);
            for (int i = 0; i < count; i++) {
                netudp_server_send(server, c, 0, msgs[i]->data,
                                   msgs[i]->size, NETUDP_SEND_UNRELIABLE);
                netudp_message_release(msgs[i]);
            }
        }
    }

    netudp_server_stop(server);
    netudp_server_destroy(server);
    netudp_term();
}
```

### Zero-Copy Buffer Send

```c
netudp_buffer_t* buf = netudp_server_acquire_buffer(server);
netudp_buffer_write_u8(buf, 0x01);       // packet type
netudp_buffer_write_f32(buf, player_x);
netudp_buffer_write_f32(buf, player_y);
netudp_buffer_write_f32(buf, player_z);
netudp_server_send_buffer(server, client, channel, buf, NETUDP_SEND_UNRELIABLE);
// buffer auto-returned to pool after flush
```

### Connect Token Generation (Backend)

```c
uint8_t token[2048];
const char* servers[] = {"game-server.example.com:27015"};

netudp_generate_connect_token(
    1, servers,            // server addresses
    300,                   // expire in 5 minutes
    10,                    // connection timeout 10s
    player_id,             // unique client ID
    0x1234567890ABCDEF,    // protocol ID
    private_key,           // 32-byte shared key
    user_data,             // 256 bytes app data (or NULL)
    token                  // output: 2048 bytes
);
// Send token to client via HTTPS
```

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│                  Application                     │
├─────────────────────────────────────────────────┤
│              netudp Public API                   │
│  netudp_server_create / netudp_client_connect    │
│  netudp_server_send / netudp_server_receive      │
├─────────────────────────────────────────────────┤
│            Channel Layer                         │
│  reliable_ordered | reliable_unordered | unreliable│
├─────────────────────────────────────────────────┤
│         Compression Layer (optional)              │
│  netc: stateful (reliable) | stateless (unreliable)│
├─────────────────────────────────────────────────┤
│          Reliability Engine                       │
│  sequence numbers | ack bitmask | retransmit     │
├─────────────────────────────────────────────────┤
│        Fragmentation Layer                        │
│  split | reassemble | fragment-level retransmit  │
├─────────────────────────────────────────────────┤
│          Encryption Layer                         │
│  AEAD (ChaCha20-Poly1305 or AES-256-GCM)        │
├─────────────────────────────────────────────────┤
│         Connection Manager                        │
│  handshake | heartbeat | timeout | bandwidth     │
├─────────────────────────────────────────────────┤
│            Socket Layer                           │
│  platform UDP socket abstraction                 │
└─────────────────────────────────────────────────┘
```

---

## Performance Targets

| Metric | Target | Measurement |
|--------|-------:|-------------|
| Packets per second (64B, encrypted) | ≥ 2M PPS | `bench_pps` |
| End-to-end latency (loopback) | p99 ≤ 5 µs | `bench_latency` |
| Memory (1024 connections) | ≤ 100 MB | `bench_memory` |
| Zero-GC compliance | 0 allocations after init | Static analysis + runtime assert |
| Compression ratio (64B game packets) | ≤ 0.76 (netc) | `bench_compression` |
| SIMD improvement | ≥ 20% over scalar | `bench_simd_compare` |
| Build time (clean, Release) | ≤ 30 seconds | CI measurement |
| Test coverage | ≥ 90% line coverage | gcov / llvm-cov |

---

## SDK Wrappers

### C++ SDK (C++17)

RAII, move-only wrappers in `namespace netudp`. Header-only via `<netudp.hpp>`.

```cpp
#include <netudp.hpp>

netudp::Server server("0.0.0.0:27015", config, 0.0);
server.start(1024);

server.on_connect([](int client, uint64_t id, std::span<const uint8_t> user_data) {
    printf("Player %llu joined\n", id);
});

// Zero-copy fluent buffer send
netudp::BufferWriter(server)
    .u8(0x01).f32(x).f32(y).f32(z)
    .send(client, 1, NETUDP_SEND_UNRELIABLE);

// Receive with zero-allocation callback
server.receive(client, 64, [](netudp::Message&& msg) {
    netudp::BufferReader reader(msg);
    uint8_t type = reader.u8();
    float x = reader.f32();
});
```

### Unreal Engine 5

```cpp
UNetudpSubsystem* Netudp = GetGameInstance()->GetSubsystem<UNetudpSubsystem>();
Netudp->StartServer(7777, 64);
Netudp->OnClientConnected.AddDynamic(this, &AMyGameMode::OnPlayerJoined);
Netudp->Send(ClientIndex, Channel, Data, /*bReliable=*/true);
```

### Unity (C#)

```csharp
var server = new NetudpServer();
server.Start("::", 7777, 64, config);
server.OnClientConnected += (client, id) => SpawnPlayer(client);

// Zero-GC send with NativeArray
server.Send(client, channel, nativeData, flags: 0);
```

### Godot 4 (GDScript)

```gdscript
var server = NetudpServer.new()
server.start("0.0.0.0", 7777, 64)
server.client_connected.connect(_on_client_connected)
server.data_received.connect(_on_data_received)

func _process(delta):
    server.update(Time.get_unix_time_from_system())
```

See [sdk/](sdk/) for full API references per engine.

---

## Project Structure

```
netudp/
├── include/netudp/       # Public API headers (extern "C")
│   ├── netudp.h          # Main API (server, client, send, recv)
│   ├── netudp_types.h    # Public types and constants (POD structs)
│   ├── netudp_buffer.h   # Buffer read/write helpers
│   ├── netudp_token.h    # Connect token generation
│   └── netudp_config.h   # Compile-time configuration
├── src/                  # C++17 implementation (internal)
│   ├── core/             # Pool<T>, FixedRingBuffer, FixedHashMap, clock
│   ├── socket/           # Platform socket abstraction (Win/Linux/Mac)
│   ├── connection/       # Connection state machine, handshake, tokens
│   ├── channel/          # Channel types (reliable, unreliable, sequenced)
│   ├── reliability/      # Sequence, ack bits, RTT, retransmission
│   ├── fragment/         # Fragmentation and reassembly
│   ├── crypto/           # ChaCha20-Poly1305, AES-GCM, CRC32C
│   ├── simd/             # SIMD dispatch (generic, SSE4.2, AVX2, NEON)
│   ├── server.cpp        # Server implementation
│   ├── client.cpp        # Client implementation
│   └── api.cpp           # extern "C" wrappers → C++ internals
├── sdk/                  # Engine bindings
│   ├── cpp/              # C++ header-only wrapper (RAII, span, move)
│   ├── uzengine/         # UzmiGames Engine subsystem
│   ├── unreal/           # Unreal Engine 5 plugin
│   ├── unity/            # Unity C# P/Invoke bindings
│   └── godot/            # Godot 4 GDExtension
├── bench/                # Micro-benchmarks (PPS, latency, SIMD)
├── tests/                # Google Test suites
├── examples/             # Usage examples (echo, chat, stress test)
├── docs/                 # Design documentation + analysis
│   ├── architecture.md   # Detailed architecture document
│   ├── PRD.md            # Product requirements
│   ├── DAG.md            # Dependency graph
│   ├── ROADMAP.md        # Release roadmap
│   └── specs/            # 21 technical specifications
├── cmake/                # CMake modules (Zig toolchain)
└── CMakeLists.txt        # Root build file
```

---

## Documentation

- [Architecture](docs/architecture.md) — Detailed design document with layer descriptions and data structures
- [PRD](docs/PRD.md) — Product requirements, success metrics, functional requirements
- [Specs](docs/specs/README.md) — 21 technical specifications (00-20) with SHALL/MUST requirements and Given/When/Then scenarios
- [DAG](docs/DAG.md) — Dependency graph with ~70 tasks in topological order
- [Roadmap](docs/ROADMAP.md) — Release timeline from v0.1.0 to v1.2.0

---

## Roadmap

| Version | Milestone | Status |
|---------|-----------|--------|
| v0.1.0 | Foundation + Encrypted Connections | In Progress |
| v0.2.0 | Reliability + Channels (4 types, RTT, retransmit) | Planned |
| v0.3.0 | Fragmentation + Multi-Frame Pipeline | Planned |
| v0.4.0 | Compression (netc) + Stats + DDoS | Planned |
| v0.5.0 | Benchmarks + Network Simulator | Planned |
| v1.0.0 | Production Ready (batch I/O, examples, docs) | Planned |
| v1.1.0 | SDK: C++ Wrapper + UzEngine | Planned |
| v1.2.0 | SDK: Unreal + Unity + Godot | Planned |

---

## Design Provenance

This architecture synthesizes the best patterns from seven analyzed implementations:

| Source | What we adopt |
|--------|---------------|
| **netcode.io** (Glenn Fiedler) | Connect tokens, challenge/response handshake, opaque API, custom allocator |
| **Valve GNS** (CS2/Dota2) | Ack vectors, multi-frame packets, Nagle batching, priority+weight lanes, stats, RTT from ack delay |
| **Unreal Engine 5.6** | Dual-layer reliability, stateless handshake, DDoS escalation, sequence window protection |
| **netc** (UzmiGames) | Purpose-built network compression (tANS, LZP, delta, SIMD) |
| **ToS2/Server1** (C#) | Packet batching, thread-local buffer pool, CRC32C hw acceleration |
| **ToS-Server-5** (C#) | ChaCha20-Poly1305, HMAC cookies, replay window, token bucket |
| **tos-mmorpg-server** (TS) | Default-on encryption validation, batching universality |

See [docs/analysis/](docs/analysis/) for detailed findings per implementation.

---

## Non-Goals

- TCP emulation (use TCP if you need full stream semantics)
- HTTP / WebSocket / WebRTC support
- Application-level serialization (bring your own)
- Game-specific logic (entities, replication, RPCs)
- P2P / NAT traversal / STUN / ICE
- Matchmaking / lobby / session management

---

## Contributing

Pull requests welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

---

## License

Apache License 2.0 — see [LICENSE](LICENSE).

Copyright 2026 UzmiGames

---

## Technical References

| Reference | Used for |
|-----------|----------|
| Glenn Fiedler, *[netcode.io](https://github.com/networkprotocol/netcode)* | Connect token protocol, handshake design |
| Glenn Fiedler, *[reliable.io](https://github.com/networkprotocol/reliable)* | Packet ack bitmask, sequence numbers |
| Valve, *[GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)* | Multi-frame packets, ack delay RTT, priority lanes |
| D.J. Bernstein, *[ChaCha20-Poly1305](https://tools.ietf.org/html/rfc8439)* (RFC 8439) | AEAD encryption |
| Frank Denis, *[Monocypher](https://monocypher.org/)* | Vendored crypto implementation |
| UzmiGames, *[netc](https://github.com/uzmigames/netc)* | Network packet compression (tANS + LZP + delta) |
| Epic Games, *Unreal Engine 5 — NetDriver / PacketHandler* | Dual-layer reliability, DDoS escalation patterns |
