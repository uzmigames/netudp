# Changelog

All notable changes to this project will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.0-dev] - Unreleased

### Added
- Project skeleton: CMake 3.20+, C++17, static library target
- CMakePresets.json (debug, release, relwithdebinfo, cross-linux via Zig CC)
- Zig CC cross-compilation toolchain (cmake/zig-toolchain.cmake)
- Google Test via FetchContent (v1.14.0)
- GitHub Actions CI (Windows/MSVC, Linux/GCC, macOS/Clang)
- Public header structure: netudp.h, netudp_types.h, netudp_config.h, netudp_buffer.h, netudp_token.h
- `netudp_init()` / `netudp_term()` lifecycle with idempotent double-init/term
- `netudp_simd_level()` query with runtime CPUID detection
- Platform detection macros (src/core/platform.h)
- clang-tidy and clang-format configuration
- All public API function declarations with link stubs
- SIMD runtime detection: CPUID (SSE4.2, AVX2) on x86-64, compile-time NEON on ARM64
- SIMD dispatch table: CRC32C, NT memcpy, ack scan, replay check, fragment bitmask, address compare
- SIMD implementations: generic scalar, SSE4.2, AVX2, NEON (4 ISA backends)
- Pool\<T\>: zero-GC object pool with O(1) acquire/release via intrusive free-list
- FixedRingBuffer\<T,N\>: power-of-2 circular buffer with FIFO and sequence access
- FixedHashMap\<K,V,N\>: open-addressing hash map with FNV-1a, linear probing
- Custom allocator interface (Allocator struct with alloc/free function pointers)
- Address parsing: IPv4 ("1.2.3.4:port") and IPv6 ("[::1]:port") with validation
- Address formatting, SIMD-accelerated comparison, FNV-1a hashing
- Platform socket abstraction: Windows (Winsock2) and Unix (BSD sockets)
- Non-blocking UDP sockets, SO_SNDBUF/RCVBUF 4MB, IPv6 dual-stack (V6ONLY=0)
- Echo integration test: raw UDP send/recv using Pool + Socket + Address
