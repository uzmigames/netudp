# netudp

High-performance, zero-GC UDP networking library for game servers.

## Overview

**netudp** is a C++17 library with `extern "C"` public API, designed for real-time game server networking. Zero garbage collection, SIMD-accelerated, with reliable and unreliable UDP communication at sub-millisecond latency. Built for dedicated servers handling thousands of concurrent players at 100K+ packets/second.

**SDK targets:** UzmiGames Engine (native C++), Unreal Engine 5, Unity, Godot 4.

### Key Features

- **Zero-GC** — no allocations after init. All memory pre-allocated from pools. Runs indefinitely without touching the heap.
- **SIMD-accelerated** — runtime dispatch to SSE4.2 / AVX2 / NEON for crypto, CRC32C, buffer ops, ack scanning, and more
- **Connect tokens** — secure authentication via encrypted tokens (netcode.io protocol). No online key exchange needed.
- **4 channel types** — unreliable, unreliable sequenced, reliable ordered, reliable unordered. Priority + weight scheduling.
- **Reliability** — piggybacked acks with delay field for continuous RTT. RTT-adaptive retransmission.
- **Fragmentation** — transparent split/reassemble for messages up to 512KB
- **Encryption** — ChaCha20-Poly1305 AEAD by default. AES-256-GCM compile-time option. Replay protection (256-entry window).
- **Compression** — optional [netc](https://github.com/uzmigames/netc) integration (35-67% bandwidth savings on game packets)
- **Nagle + flush** — automatic batching with per-message bypass. Multi-frame packets (ack + data in one UDP packet).
- **Bandwidth control** — token bucket rate limiting + congestion avoidance
- **Comprehensive stats** — ping, quality, throughput, queue depth, compression ratio per connection/channel
- **Packet interfaces** — handler registration, lifecycle callbacks, zero-copy buffer acquire/send for game servers
- **Cross-platform** — Windows, Linux, macOS, Android, iOS via CMake + Zig CC
- **Engine SDKs** — C++ header-only wrapper, Unreal 5 plugin, Unity C# bindings, Godot 4 GDExtension

### Non-Goals

- TCP emulation (use TCP if you need full stream semantics)
- HTTP/WebSocket support
- Application-level serialization (bring your own)

## Building

### Requirements

- C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- CMake 3.20+
- Zig 0.13+ (optional, for cross-compilation via `zig c++`)
- Google Test (fetched automatically via CMake FetchContent)

### Build Commands

```bash
# Configure
cmake -B build -DCMAKE_C_COMPILER="zig cc" -DCMAKE_CXX_COMPILER="zig c++"

# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Cross-compile for Linux from Windows
cmake -B build-linux -DCMAKE_C_COMPILER="zig cc" -DCMAKE_CXX_COMPILER="zig c++" -DCMAKE_SYSTEM_NAME=Linux
cmake --build build-linux
```

## Architecture

```
┌─────────────────────────────────────────────────┐
│                  Application                     │
├─────────────────────────────────────────────────┤
│              netudp Public API                   │
│  netudp_server_create / netudp_client_connect    │
│  netudp_send / netudp_recv / netudp_poll         │
├─────────────────────────────────────────────────┤
│            Channel Layer                         │
│  reliable_ordered | reliable_unordered | unreliable│
├─────────────────────────────────────────────────┤
│          Reliability Engine                       │
│  sequence numbers | ack bitmask | retransmit     │
├─────────────────────────────────────────────────┤
│        Fragmentation Layer                        │
│  split | reassemble | MTU discovery              │
├─────────────────────────────────────────────────┤
│          Encryption Layer                         │
│  AEAD (ChaCha20-Poly1305 or AES-128-GCM)        │
├─────────────────────────────────────────────────┤
│         Connection Manager                        │
│  handshake | heartbeat | timeout | bandwidth     │
├─────────────────────────────────────────────────┤
│            Socket Layer                           │
│  platform UDP socket abstraction                 │
└─────────────────────────────────────────────────┘
```

See [docs/architecture.md](docs/architecture.md) for detailed design documentation.

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
│   ├── unreal/           # Unreal Engine 5 plugin
│   ├── unity/            # Unity C# P/Invoke bindings
│   └── godot/            # Godot 4 GDExtension
├── bench/                # Micro-benchmarks (PPS, latency, SIMD)
├── tests/                # Google Test suites
├── examples/             # Usage examples
├── docs/                 # Design documentation + analysis
├── cmake/                # CMake modules
└── CMakeLists.txt        # Root build file
```

## Quick Example

```c
#include <netudp/netudp.h>

// Server
netudp_config_t config = netudp_default_config();
config.max_connections = 1024;
config.port = 27015;

netudp_server_t* server = netudp_server_create(&config);

while (running) {
    netudp_event_t event;
    while (netudp_server_poll(server, &event)) {
        switch (event.type) {
            case NETUDP_EVENT_CONNECT:
                printf("Client connected: %u\n", event.connection_id);
                break;
            case NETUDP_EVENT_DATA:
                // event.data, event.data_len, event.channel
                netudp_server_send(server, event.connection_id,
                                   NETUDP_CHANNEL_UNRELIABLE,
                                   response, response_len);
                break;
            case NETUDP_EVENT_DISCONNECT:
                printf("Client disconnected: %u\n", event.connection_id);
                break;
        }
    }
}

netudp_server_destroy(server);
```

## License

Apache License 2.0 — see [LICENSE](LICENSE).

Copyright 2026 UzmiGames
