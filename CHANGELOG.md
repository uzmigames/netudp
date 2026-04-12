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
- Vendored monocypher: ChaCha20-Poly1305 AEAD, XChaCha20-Poly1305, BLAKE2b
- AEAD encrypt/decrypt wrappers (24-byte nonce for monocypher compatibility)
- XChaCha20 encrypt/decrypt for connect token encryption (HChaCha20 subkey derivation)
- CRC32C wrapper with SIMD dispatch (fixed Castagnoli table for generic fallback)
- CSPRNG: BCryptGenRandom (Windows), /dev/urandom (Linux), arc4random (macOS)
- Replay protection: 256-entry window on 64-bit nonce counter (EMPTY_SLOT sentinel)
- KeyEpoch: per-connection Tx/Rx keys, 64-bit nonce counter, byte tracking
- Packet-level encrypt/decrypt with AAD (version + protocol_id + prefix = 22 bytes)
- Connect token generation: serialize private data, XChaCha20 encrypt, 2048-byte output
- Connect token validation: decrypt, check version/protocol/expiry/server address
- Token fingerprint: BLAKE2b keyed hash (8 bytes) for anti-replay
- Per-IP rate limiter: token bucket (60/s, burst 10, 30s expiry, 4096-entry hash map)
- Client state machine: 10 states (-6 to 3), multi-server fallback on timeout
- Server lifecycle: create/start/stop/update/destroy, connection slots with generation counter
- Client lifecycle: create/connect/update/disconnect/destroy
- Simplified handshake: CONNECTION_REQUEST → KEEPALIVE (direct connection)
- Server: receive loop with rate limiting, connection timeout detection
- netudp_server_send/broadcast/broadcast_except (encrypted, simplified)
- netudp_client_send (encrypted)
- PacketTracker: 16-bit send sequence, piggybacked ack + 32-bit ack_bits, ack_delay_us
- AckFields serialization (8 bytes): ack(2) + ack_bits(4) + ack_delay_us(2)
- Sequence window protection: 33-packet window matching ack_bits coverage
- RTT estimation: RFC 6298 (SRTT, RTTVAR, RTO), ack delay subtracted, [100ms, 2000ms] clamp
- 4 channel types: unreliable, unreliable_sequenced, reliable_ordered, reliable_unordered
- Per-channel send queue (256 entries), Nagle timer, NO_NAGLE/NO_DELAY bypass, flush
- Priority + weight channel scheduler (highest priority first)
- Unreliable sequenced: drop stale packets (sequence <= last_delivered)
- ReliableChannelState: 512-entry send/recv buffers, per-channel message sequencing
- Reliable retransmission: RTT-adaptive backoff (RTO x 2^min(retry,5)), max 10 retries
- Reliable ordered delivery: buffer out-of-order, deliver contiguous from recv_seq
- Reliable unordered delivery: immediate delivery with duplicate detection
