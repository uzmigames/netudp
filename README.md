# netudp

High-performance UDP networking library for game servers.

## Overview

**netudp** is a C/C++ library designed for real-time game server networking. It provides reliable and unreliable UDP communication with minimal latency, built for scenarios where thousands of concurrent players require sub-millisecond packet processing.

### Key Features

- **Connection management** — virtual connections over UDP with handshake, heartbeat, and timeout detection
- **Reliability layer** — selective reliable delivery with sequence numbers, acknowledgments, and retransmission
- **Unreliable fast-path** — zero-copy fire-and-forget for latency-critical data (position updates, inputs)
- **Fragmentation & reassembly** — automatic splitting of messages that exceed MTU
- **Channel system** — multiple logical channels per connection (reliable ordered, reliable unordered, unreliable)
- **Encryption** — built-in AEAD encryption for all traffic after handshake
- **Bandwidth control** — per-connection send rate limiting and congestion avoidance
- **Cross-platform** — Windows, Linux, macOS via Zig CC cross-compilation

### Non-Goals

- TCP emulation (use TCP if you need full stream semantics)
- HTTP/WebSocket support
- Application-level serialization (bring your own)

## Building

### Requirements

- CMake 3.20+
- Zig 0.13+ (used as C/C++ cross-compiler via `zig cc`)
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
├── include/netudp/       # Public API headers
│   ├── netudp.h          # Main API (server, client, send, recv)
│   ├── types.h           # Public types and constants
│   └── config.h          # Compile-time configuration
├── src/                  # Implementation
│   ├── socket/           # Platform socket abstraction
│   ├── connection/       # Connection manager, handshake, heartbeat
│   ├── channel/          # Channel types (reliable, unreliable)
│   ├── reliability/      # Sequence numbers, acks, retransmission
│   ├── fragment/         # Fragmentation and reassembly
│   ├── crypto/           # Encryption (AEAD)
│   └── core/             # Memory allocator, buffer pool, time
├── tests/                # Google Test suites
├── examples/             # Usage examples
├── docs/                 # Design documentation
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
