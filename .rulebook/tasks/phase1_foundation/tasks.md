## 1. SIMD Detection & Dispatch (P1-A) — spec 01
- [x] 1.1 Create src/simd/netudp_simd.h: SimdOps dispatch table with 10 function pointers
- [x] 1.2 Create src/simd/netudp_simd_detect.cpp: CPUID x86-64 (SSE4.2, AVX2), compile-time NEON
- [x] 1.3 Create src/simd/simd_generic.cpp: scalar fallback (CRC32C table, memcpy, popcount, etc.)
- [x] 1.4 Create src/simd/simd_sse42.cpp: _mm_crc32_u64, NT stores (aligned), _mm_popcnt, _tzcnt
- [x] 1.5 Create src/simd/simd_avx2.cpp: _mm256 NT stores, 4-wide replay check, accumulate
- [x] 1.6 Create src/simd/simd_neon.cpp: __crc32cd, vceqq, vcntq, vaddq
- [x] 1.7 Updated CMakeLists.txt: per-file compile flags (-msse4.2, -mavx2 -mbmi2)
- [x] 1.8 Tests: 14 SIMD tests — detection, CRC32C vectors, memcpy, ack scan, popcount, replay, fragments, accumulate, addr_equal

## 2. Platform Socket Abstraction (P1-B) — spec 02
- [x] 2.1 Create src/socket/socket.h: Socket struct, create/send/recv/destroy declarations
- [x] 2.2 Implemented Windows Winsock2: WSAStartup, ioctlsocket FIONBIO, closesocket
- [x] 2.3 Implemented Unix BSD sockets: fcntl O_NONBLOCK, close
- [x] 2.4 IPv6 dual-stack: IPV6_V6ONLY=0, sockaddr_storage conversion
- [x] 2.5 Tests: 4 socket tests — create/destroy, send-to-self, non-blocking, two-socket comm

## 3. Zero-GC Memory & Pools (P1-C) — spec 03
- [x] 3.1 Create src/core/allocator.h: Allocator struct with alloc/free + default_allocator()
- [x] 3.2 Create src/core/pool.h: Pool<T> with intrusive free-list, O(1) acquire/release
- [x] 3.3 Create src/core/ring_buffer.h: FixedRingBuffer<T,N> power-of-2 circular buffer
- [x] 3.4 Create src/core/hash_map.h: FixedHashMap<K,V,N> open addressing, FNV-1a
- [x] 3.5 Tests: 15 container tests — Pool (init, exhaust, zero-init), RingBuffer (FIFO, full, wrap, clear), HashMap (insert, find, remove, update, collision, full, for_each)

## 4. Address Parsing & Comparison (P1-D) — spec 02
- [x] 4.1 Create src/core/address.h: address_zero(), address_data_len(), address_hash()
- [x] 4.2 Implement netudp_parse_address(): IPv4 + IPv6 with :: expansion
- [x] 4.3 Implement netudp_address_to_string(): format with overflow protection
- [x] 4.4 Implement netudp_address_equal(): type-aware, SIMD via g_simd->addr_equal
- [x] 4.5 Implement address_hash(): FNV-1a over type-relevant bytes + port
- [x] 4.6 Tests: 18 address tests — parse IPv4/IPv6, invalid, format, equal, hash, round-trip

## 5. Basic Send/Recv Integration (P1-E)
- [x] 5.1 Raw UDP send via socket_send + address
- [x] 5.2 Raw UDP recv via socket_recv into Pool buffer
- [x] 5.3 Integration test: echo server (send Hello → recv → echo back → verify) + multi-message pool test

## 6. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 6.1 Updated CHANGELOG.md with all Phase 1 additions
- [x] 6.2 All tests pass: 59/59 (14 SIMD + 4 socket + 15 containers + 18 address + 2 echo + 6 lifecycle)
- [x] 6.3 Build + clang-tidy: zero warnings
