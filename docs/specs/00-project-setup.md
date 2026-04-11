# Spec 00 — Project Setup

## Requirements

### REQ-00.1: Build System
The project SHALL use CMake 3.20+ as build system.
The project SHALL support Zig CC as cross-compiler via toolchain file.
The project SHALL support native compilers (GCC 9+, Clang 10+, MSVC 2019+).
The project SHALL compile as C++17 (`-std=c++17`).

### REQ-00.2: Dependencies
The project SHALL fetch Google Test via CMake FetchContent.
The project SHALL vendor crypto primitives (monocypher or libsodium subset).
The project SHALL optionally link netc for compression (`NETUDP_ENABLE_COMPRESSION`).
The project SHALL have ZERO required external dependencies at runtime.

### REQ-00.3: Public Headers
The project SHALL expose public API via `include/netudp/`:
- `netudp.h` — core API (server, client, send, recv)
- `netudp_types.h` — all public types (POD structs, enums, constants)
- `netudp_buffer.h` — buffer read/write helpers
- `netudp_token.h` — connect token generation
- `netudp_config.h` — compile-time configuration defines

All public headers SHALL use `extern "C"` linkage.
All public types SHALL be POD structs (no virtual functions, no constructors).

### REQ-00.4: CI
The project SHALL have GitHub Actions CI running on:
- Windows x64 (MSVC)
- Ubuntu x64 (GCC)
- macOS ARM64 (Clang)

CI SHALL run: build, tests, static analysis (clang-tidy).
CI SHALL fail on any test failure or clang-tidy error.

### REQ-00.5: Static Analysis
The project SHALL use clang-format with a checked-in `.clang-format`.
The project SHALL use clang-tidy with rules prohibiting:
- `new`/`delete`/`malloc`/`free` in `src/` (outside `init`/`destroy`)
- `std::string`, `std::vector`, `std::map` in `src/` hot path
- Exceptions (`throw`)
- RTTI (`dynamic_cast`, `typeid`)

### REQ-00.6: Directory Structure

```
include/netudp/           # Public API (extern "C")
src/
  core/                   # Pool, RingBuffer, HashMap, clock, assert
  socket/                 # Platform socket abstraction
  connection/             # State machine, tokens, handshake, rate limiter
  channel/                # Channel types, Nagle, priority scheduler
  reliability/            # Sequence, ack, RTT, retransmission
  fragment/               # Split, reassemble, bitmask
  crypto/                 # AEAD, CRC32C, random
  simd/                   # Detection, dispatch, per-ISA implementations
  server.cpp              # Server implementation
  client.cpp              # Client implementation
  api.cpp                 # extern "C" wrappers
sdk/                      # Engine bindings (Phase 8)
bench/                    # Micro-benchmarks
tests/                    # Google Test
examples/                 # Echo, chat, stress
docs/                     # Architecture, specs, analysis
cmake/                    # Toolchain files, modules
```

### REQ-00.7: Initialization

```cpp
// Public API
int  netudp_init(void);   // Returns NETUDP_OK or NETUDP_ERROR
void netudp_term(void);   // Cleanup, safe to call multiple times
```

`netudp_init()` SHALL:
1. Initialize platform sockets (Winsock on Windows)
2. Detect SIMD level (CPUID on x86, compile-time on ARM)
3. Set function pointer dispatch table
4. Initialize vendored crypto library
5. Return `NETUDP_OK` on success

`netudp_term()` SHALL:
1. Deinitialize platform sockets
2. Zero sensitive memory (keys)
3. Be safe to call if `netudp_init()` was never called

## Scenarios

#### Scenario: Fresh build on Windows
Given a clean checkout on Windows x64 with MSVC 2022
When `cmake -B build && cmake --build build`
Then build succeeds with zero warnings at `/W4`
And all tests pass

#### Scenario: Cross-compile for Linux from Windows
Given Windows host with Zig 0.13+ installed
When `cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/zig-linux-x64.cmake`
Then build produces `libnetudp.so` for linux-x64

#### Scenario: Zero-alloc enforcement
Given a PR that adds `new` in `src/channel/reliable_ordered.cpp`
When CI runs clang-tidy
Then CI fails with error indicating prohibited allocation in hot path
