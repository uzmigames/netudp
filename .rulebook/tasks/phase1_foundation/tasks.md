## 1. SIMD Detection & Dispatch (P1-A) — spec 01
- [ ] 1.1 Create src/simd/netudp_simd.h: SimdLevel enum (Generic=0, SSE42=1, AVX2=2, NEON=3, AVX512=4), SimdOps struct with function pointers (crc32c, memcpy_nt, memset_zero, ack_bits_scan, popcount32, replay_check, fragment_bitmask_complete, fragment_next_missing, accumulate_u64, addr_equal), extern const SimdOps* g_simd
- [ ] 1.2 Create src/simd/netudp_simd_detect.cpp: CPUID-based detection for x86-64 (SSE4.2, AVX2), compile-time NEON for ARM64, set g_simd pointer at init, netudp_simd_level() public query function
- [ ] 1.3 Create src/simd/simd_generic.cpp: scalar fallback implementations for ALL SimdOps functions — slicing-by-16 CRC32C table, memcpy loop, bit-by-bit ack scan, linear replay check, scalar popcount
- [ ] 1.4 Create src/simd/simd_sse42.cpp (-msse4.2 -mpopcnt): _mm_crc32_u64 CRC32C, _mm_stream_si128 non-temporal memcpy with _mm_sfence, _mm_popcnt_u64 + _tzcnt_u64 ack scan, _mm_cmpeq_epi64 replay check, _mm_cmpeq_epi8 + _mm_movemask address compare
- [ ] 1.5 Create src/simd/simd_avx2.cpp (-mavx2 -mbmi2): _mm256_stream_si256 memcpy, _mm256_cmpeq_epi64 replay (4x parallel), _mm256_cmpeq_epi8 address compare, _pdep_u64 ack scan optimization
- [ ] 1.6 Create src/simd/simd_neon.cpp: __crc32cd ARM CRC, vst1q_u8 memcpy, vceqq_u64 replay check, vcntq_u8 popcount, vceqq_u8 address compare, vclz_u32 ack scan
- [ ] 1.7 Update CMakeLists.txt: per-file compile flags (-msse4.2 for sse42.cpp, -mavx2 -mbmi2 for avx2.cpp), conditional ARM NEON, NETUDP_ENABLE_AVX512 option (OFF by default)
- [ ] 1.8 Tests: test_simd.cpp — verify detection returns valid level, verify CRC32C matches known vectors, verify memcpy_nt copies correctly, verify ack_bits_scan finds correct indices

## 2. Platform Socket Abstraction (P1-B) — spec 02
- [ ] 2.1 Create src/socket/socket.h: Socket struct (platform handle), socket_create/send/recv/destroy/send_batch/recv_batch declarations
- [ ] 2.2 Create src/socket/socket_win.cpp: Winsock2 implementation — WSAStartup in init, socket() + bind() + ioctlsocket(FIONBIO), sendto/recvfrom, setsockopt SO_SNDBUF/SO_RCVBUF 4MB, closesocket
- [ ] 2.3 Create src/socket/socket_unix.cpp: BSD socket implementation — socket() + bind() + fcntl(O_NONBLOCK), sendto/recvfrom, setsockopt SO_SNDBUF/SO_RCVBUF 4MB, SO_REUSEADDR, close
- [ ] 2.4 Implement dual-stack IPv6: IPV6_V6ONLY=0 when binding to "::", handle IPv4-mapped IPv6 addresses in recv
- [ ] 2.5 Tests: test_socket.cpp — create+bind on localhost, send packet to self, recv packet, verify payload matches, test non-blocking returns 0 when no data

## 3. Zero-GC Memory & Pools (P1-C) — spec 03
- [ ] 3.1 Create src/core/allocator.h: netudp_allocator_t struct {void* ctx, alloc fn, free fn}, default_allocator using malloc/free
- [ ] 3.2 Create src/core/pool.h: Pool<T> template — pre-allocate N elements contiguously, intrusive free-list (next pointer in freed elements), O(1) acquire/release, capacity/available getters, zero-alloc after init
- [ ] 3.3 Create src/core/ring_buffer.h: FixedRingBuffer<T,N> — fixed-capacity circular buffer, push_back/pop_front/operator[], size/capacity/full/empty, compile-time N (power of 2 enforced via static_assert)
- [ ] 3.4 Create src/core/hash_map.h: FixedHashMap<K,V,N> — open addressing (linear probing), fixed capacity, insert/find/remove, hash function customizable, zero-init on construction, support Address as key type
- [ ] 3.5 Tests: test_pool.cpp — acquire all N, verify N+1 returns null, release one + re-acquire succeeds; test_ring_buffer.cpp — push N, verify FIFO order, overflow behavior; test_hash_map.cpp — insert/find/remove, collision handling, full map behavior

## 4. Address Parsing & Comparison (P1-D) — spec 02
- [ ] 4.1 Create src/core/address.h: Address struct (union ipv4[4]/ipv6[8], port uint16, type uint8), ensure zero-initialization of full union on construction
- [ ] 4.2 Implement netudp_parse_address(): parse "1.2.3.4:27015" (IPv4) and "[::1]:27015" (IPv6), return NETUDP_ERROR_INVALID_PARAM on bad input
- [ ] 4.3 Implement netudp_address_to_string(): format Address back to string, buffer overflow protection
- [ ] 4.4 Implement netudp_address_equal(): compare only type-relevant bytes (4 for IPv4, 16 for IPv6) + port + type, SIMD-accelerated via g_simd->addr_equal
- [ ] 4.5 Implement address_hash(): FNV-1a hash over type-relevant bytes + port, for use as FixedHashMap key
- [ ] 4.6 Tests: test_address.cpp — parse IPv4, parse IPv6, parse invalid returns error, equal/not-equal, hash consistency, round-trip parse→format→parse

## 5. Basic Send/Recv Integration (P1-E)
- [ ] 5.1 Implement raw unencrypted UDP send: socket_send with address + data from Pool<T> buffer
- [ ] 5.2 Implement raw unencrypted UDP recv: socket_recv into Pool<T> buffer, return Address + data + len
- [ ] 5.3 Integration test: echo server — server binds port, client sends "Hello", server receives + echoes back, client verifies, all using Pool<T> buffers and Address type

## 6. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 6.1 Update CHANGELOG.md: "Added: SIMD detection (SSE4.2/AVX2/NEON), platform sockets, Pool<T>, FixedRingBuffer, FixedHashMap, address parsing"
- [ ] 6.2 All tests pass: test_simd, test_socket, test_pool, test_ring_buffer, test_hash_map, test_address, test_echo_integration
- [ ] 6.3 Run type-check (tsc --noEmit equivalent: cmake --build build) + clang-tidy — zero warnings
