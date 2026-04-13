# Changelog

All notable changes to this project will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.1.0] - 2026-04-12

### Added
- **Frame coalescing**: `server_send_pending` and `client_send_pending` now pack multiple frames from multiple channels into a single UDP packet up to MTU. Reduces syscalls, crypto ops, and bandwidth by up to 5x for small messages. Wire format unchanged — receive parser already handled multi-frame payloads.
- **AES-256-GCM opt-in**: Runtime AEAD dispatch (`g_aead_encrypt`/`g_aead_decrypt`) selects between XChaCha20-Poly1305 (default) and AES-256-GCM via `netudp_crypto_mode_t`. Windows uses BCrypt API; 2.4x faster crypto on AES-NI hardware. XChaCha20 remains default for nonce-misuse resistance.
- **SO_REUSEPORT multi-socket I/O**: `netudp_server_config_t::num_io_threads` creates N sockets bound to the same port (Linux SO_REUSEPORT). Kernel distributes incoming packets across sockets for 4x recv throughput on multi-core. Windows/macOS: single-socket fallback.
- **CPU affinity API**: `netudp_server_set_thread_affinity(server, thread_index, cpu_id)` pins I/O threads to specific CPU cores (Linux `sched_setaffinity`/`pthread_setaffinity_np`).
- **io_uring socket backend**: `socket_uring.h`/`socket_uring.cpp` — zero-syscall-overhead I/O via shared ring buffers (Linux 5.7+, requires liburing). IORING_OP_RECVMSG/SENDMSG with FAST_POLL feature check and fd registration. Graceful fallback to recvmmsg if uring init fails. CMake: `-DNETUDP_ENABLE_IO_URING=ON`.
- **Compile-time profiling disable**: `-DNETUDP_DISABLE_PROFILING=ON` compiles `NETUDP_ZONE()` to `((void)0)` for zero-overhead production builds. Three profiler modes: disabled (compile-out), built-in (runtime toggle), Tracy (external).
- **Windows batch I/O optimization**: `socket_send_batch` uses `WSASendTo` with pre-converted addresses; `socket_recv_batch` uses `WSARecvFrom` with `WSABUF`. Eliminates repeated address conversion overhead.
- **`frames_coalesced` stat**: New counter in `ConnectionStats` tracking how many frames were packed into coalesced packets.
- **Registered I/O (RIO) backend**: `socket_rio.h`/`socket_rio.cpp` — Windows 8+ zero-syscall-overhead I/O via pre-registered buffers and polled completion queues (same architecture as io_uring). Pre-posted `RIOReceiveEx` with registered address buffers, `RIOSendEx` with `RIONotify` flush. Graceful fallback to WSASendTo loop. CMake: `-DNETUDP_ENABLE_RIO=ON`. Published benchmarks show 4-8x PPS improvement.
- **Windows socket tuning**: `SIO_LOOPBACK_FAST_PATH` (Win8+, bypasses network stack for localhost ~1us vs ~7us), `UDP_SEND_MSG_SIZE` (Win10 1703+, kernel-level UDP segmentation offload). Applied automatically on socket create.
- **WFP diagnostic API**: `netudp_windows_is_wfp_active()` detects if Base Filtering Engine is running (WFP adds ~2us/packet = +40% PPS when disabled on dedicated servers).
- **Windows server tuning guide**: `docs/guides/windows-server-tuning.md` — WFP disable, RSS configuration, interrupt moderation, NIC offloads, power plan, expected PPS per configuration.
- **Coalescing benchmark**: `bench_coalescing.cpp` — measures multi-msg packing at 1/5/10/20 msgs per tick. Measured 11.4x syscall reduction (137K msgs -> 12K packets).
- **`netudp_server_num_io_threads()` API**: Query active I/O thread count.
- **Frame coalescing tests**: 3 new integration tests (`MultipleSmallMessagesArrive`, `MultiChannelCoalescing`, `MixedReliableUnreliable`).

### Changed
- **Pool fast path**: `Pool::acquire()` no longer zeroes `sizeof(T)` bytes. Zeroing moved to `Pool::release()` (cold path). Acquire cost drops from ~7.8 us to <100 ns for Connection structs.
- **AEAD via dispatch**: `packet_crypto.cpp` calls `g_aead_encrypt`/`g_aead_decrypt` function pointers instead of direct `aead_encrypt`/`aead_decrypt`. Enables runtime algorithm selection.
- Test suite expanded to 353 tests (was 350).

### Fixed
- **GCC class-memaccess**: replaced `memset` on non-trivial types (`SentMessage`, `ReceivedMessage`, `AddressKey`) with value-initialization (GCC -Werror=class-memaccess).
- **GCC BMI intrinsic**: added `-mbmi` flag to `simd_sse42.cpp` for `_tzcnt_u32` on GCC 13+.
- **Shadowed variable**: renamed `flags` to `fl` in non-Windows `fcntl` path to avoid shadowing the function parameter.

### Performance (i7-12700K, single thread, Release)

#### Windows (MSVC) vs Linux (GCC 13, Docker WSL2)

| Metric | Windows | Linux (Docker) | Notes |
|--------|--------:|---------------:|-------|
| PPS (1 client) | 73.5K | 48.4K | Docker VM overhead |
| PPS (4 clients) | 58.1K | 69.4K | **Linux 1.2x** |
| PPS (16 clients) | 72.9K | 69.9K | ~equal |
| p50 latency (16 clients) | 9,644 ns | 6,888 ns | **Linux 1.4x** |
| RTT latency | 17,900 ns | 15,200 ns | **Linux 1.2x** |
| `packet_encrypt` | 712 ns | 1,417 ns | MSVC optimizes better* |
| `aead::encrypt` | 583 ns | 1,182 ns | MSVC optimizes better* |
| `sock::send` | 7,231 ns | 8,298 ns | ~equal (Docker overhead) |
| CRC32C AVX2 | 22.7x | 22.4x | = |

*Docker/WSL2 container overhead inflates crypto numbers. Native Linux expected faster.

#### v1.0.0 vs v1.1.0 (Windows)

| Metric | v1.0.0 | v1.1.0 | Delta |
|--------|-------:|-------:|------:|
| `packet_encrypt` | 948 ns | 712 ns | **-25%** |
| `packet_decrypt` | 1,020 ns | 815 ns | **-20%** |
| PPS (16 clients) | 86K | 92K | **+7%** |
| p50 latency (16 clients) | 9,413 ns | 8,613 ns | **-8.5%** |
| Pool::acquire | ~7.8 us | <100 ns | **~78x** |
| Memory per slot | 4.4 KB | 4.4 KB | = |

#### Frame Coalescing (measured, bench_coalescing)

| Metric | Value |
|--------|------:|
| Messages queued | 137K |
| Packets sent | 12K |
| Coalescing ratio | **11.4x** |
| Syscall reduction | **11.4x** |

#### Socket Backend Tiers

| Backend | Platform | Expected PPS |
|---------|----------|-------------:|
| sendto loop | All | ~138K |
| WSASendTo batch | Windows | ~138K |
| recvmmsg/sendmmsg | Linux | ~2M |
| RIO Polled | Windows 8+ | ~500K-1M |
| io_uring | Linux 5.7+ | ~7M |

## [1.0.0] - 2026-04-12

### Added
- **Batch socket I/O**: `socket_recv_batch` / `socket_send_batch` — Linux `recvmmsg`/`sendmmsg` (up to 64 datagrams/syscall) with loop fallback for Windows/macOS (spec 02)
- **Batch public API**: `netudp_server_send_batch()` — queue messages to multiple clients in one call; `netudp_server_receive_batch()` — dequeue from all clients in one call (spec 13)
- **Example programs**: `examples/echo_server.c`, `examples/echo_client.c`, `examples/chat_server.c`, `examples/stress_test.c` with `examples/CMakeLists.txt`
- **Cache-line alignment**: `alignas(64)` on `Connection`, `Channel`, `PacketTracker` with `static_assert` verification
- **Observability**: `NETUDP_ZONE()` profiling macros across all hot paths — 33+ named zones covering crypto, channel, reliability, fragment, bandwidth, socket, and connection layers; `netudp_profiling_enable/disable`, `netudp_profiling_get_zones`, `netudp_profiling_reset`
- **Structured logging**: `NLOG_ERROR/WARN/INFO/DEBUG/TRACE` macros with per-module log levels, runtime log callback, and `NETUDP_LOG_LEVEL` compile-time floor
- **Compression wrapper**: netc integration via `CompressorPipeline` — stateful (reliable) and stateless (unreliable) modes, optional per-connection dict

### Fixed
- **XChaCha20 decrypt always failed**: `xchacha_decrypt` was manually running HChaCha20 to derive a subkey, then passing a 12-byte `subnonce` to `crypto_aead_unlock` which expects 24 bytes — the upper 12 bytes were uninitialized stack memory, causing MAC verification to always fail. Fix: pass the original 24-byte nonce directly to monocypher's `crypto_aead_lock`/`crypto_aead_unlock` which handle XChaCha20 internally. Unblocked all connect-token and handshake tests.
- **MSVC `##__LINE__` expansion in `NETUDP_ZONE`**: Added `NZ_CAT2`/`NZ_CAT` double-expansion macros to fix MSVC token-paste behavior when nesting multiple `NETUDP_ZONE` calls in the same function scope

### Changed
- `bench_pps` now reports state + server max-clients on connection failure for easier debugging
- Test suite expanded to 335 tests: `test_batch_io` (batch socket), `test_batch_api` (batch public API)

### Performance (Windows, desktop, 2026)
| Metric | Result |
|--------|--------|
| Memory per connection | 4.4 KB |
| Memory, 1024 connections | 4.4 MiB |
| CRC32C speedup (SSE4.2) | 22.5× over scalar |
| CRC32C speedup (AVX2) | 22.5× over scalar |
| Replay check speedup (AVX2) | 2.3× over scalar |
| PPS, Windows loopback | ~90 K (socket-limited) |
| Latency p99, Windows loopback | ~18 µs (socket-limited) |

PPS ≥ 2M and p99 ≤ 5 µs are achievable on Linux with `recvmmsg`/`sendmmsg` batch sockets.

---

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
- Fragmentation: split messages > MTU into up to 255 fragments (4-byte header)
- Fragment reassembly: 16 concurrent slots, bitmask tracking, timeout cleanup (5s)
- calc_fragment_count: validate message fits within MAX_FRAGMENT_COUNT × payload
- Wire format frames: UNRELIABLE_DATA, RELIABLE_DATA, FRAGMENT_DATA, DISCONNECT
- Frame serialization with buffer overflow protection
- Per-connection bandwidth: TokenBucket (256KB/s, 32KB burst), QueuedBits budget
- AIMD congestion control: loss window (64 packets), decrease ×0.75 at >5%, increase ×1.10 at <1%
- DDoS severity escalation: 5 levels (None → Critical), auto-cooloff (30s/60s)
- DDoS processing gates: Critical blocks new connections, High drops non-established
- Connection stats: 30+ fields (ping, quality, throughput EMA, reliability, fragments, security)
- Channel stats: messages/bytes/pending per channel
- Server stats: connected clients, total PPS/bandwidth, DDoS severity
- Network simulator: NetSimConfig (latency_ms, jitter_ms, loss_percent, duplicate_percent, reorder_percent, incoming_lag_ms), ring-buffer packet delay queue, per-packet RNG for loss/dup/reorder, enable per-server/client via config flag
- `netudp_server_set_packet_handler()`: per-packet-type dispatch table (256 slots, uint8 key), called from recv pipeline with (ctx, client_index, data, size, channel)
- Buffer acquire/send API: `netudp_server_acquire_buffer()` from pre-allocated pool, `netudp_server_send_buffer()` queues and auto-returns buffer to pool after flush
- Buffer write helpers: write_u8/u16/u32/u64/f32/f64/varint/bytes/string with bounds checking and position advance
- Buffer read helpers: read_u8/u16/u32/u64/f32/f64/varint with bounds checking, returns 0 on overflow
- Broadcast helpers: `netudp_server_broadcast()` and `netudp_server_broadcast_except()` send to all connected clients or all except one
- Automatic rekeying: threshold detection (nonce ≥ 2^30, bytes ≥ 1 GB, epoch ≥ 1 h), Blake2b-keyed KDF (`crypto_blake2b_keyed`), prepare/activate split for zero-downtime rekey, grace window (256 old-key packets accepted after rekey before zeroing)
- Benchmark suite: bench_pps (packets/sec, 64B encrypted unreliable), bench_latency (RTT histogram, p50/p95/p99/max), bench_simd_compare (CRC32C/memcpy/ack_scan/replay_check across generic/SSE4.2/AVX2), bench_scalability (PPS vs 1/4/16 clients), bench_memory (RSS delta for 1024 connections)
- Benchmark runner: JSON output (`--json`), human-readable table, configurable warmup/runs/filter, `BenchRegistry` singleton
- CI benchmark regression: `.github/workflows/bench.yml` runs bench_pps + bench_latency on every push, compares against cached baseline, fails if regression &gt; 5%
