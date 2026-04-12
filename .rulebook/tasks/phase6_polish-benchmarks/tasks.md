## 1. Network Simulator (P6-A) — spec 15
- [ ] 1.1 Create src/sim/network_sim.h/.cpp: NetworkSimulator struct — configurable params: latency_ms (min/max), jitter_ms, loss_percent (0-100), duplicate_percent, reorder_percent, incoming_lag_ms
- [ ] 1.2 Implement packet delay queue: ring buffer of {data, len, deliver_time}, insert with randomized delay (latency + jitter), poll delivers packets whose time has passed
- [ ] 1.3 Implement loss/dup/reorder: on packet submit, roll RNG — loss: drop, duplicate: insert twice, reorder: randomize deliver_time within window
- [ ] 1.4 Wire simulator between socket layer and crypto layer: interceptor that can be enabled per server/client via config flag
- [ ] 1.5 Tests: test_network_sim.cpp — 100% loss drops all, 0% loss passes all, 50ms latency delays correctly, duplicate generates 2 copies

## 2. Packet Interfaces + Buffer API (P6-B) — spec 13
- [ ] 2.1 Implement netudp_server_set_packet_handler(): register callback (fn, ctx) per uint16 packet_type, dispatched during recv when first byte of message matches type
- [ ] 2.2 Implement connection lifecycle callbacks: on_connect (ctx, client_index, client_id, user_data[256]) and on_disconnect (ctx, client_index, reason) set via server config, called during update()
- [ ] 2.3 Implement netudp_server_acquire_buffer(): return netudp_buffer_t* from pre-allocated pool, buffer has data pointer + capacity + position
- [ ] 2.4 Implement netudp_server_send_buffer(): queue buffer for send on channel with flags, auto-return to pool after flush
- [ ] 2.5 Implement buffer write helpers: write_u8/u16/u32/u64/f32/f64/varint/bytes/string — write to buffer at position, advance position, bounds check
- [ ] 2.6 Implement buffer read helpers: read_u8/u16/u32/u64/f32/f64/varint — read from buffer at position, advance, bounds check, return 0 on overflow
- [ ] 2.7 Implement broadcast helpers: netudp_server_broadcast(channel, data, bytes, flags), netudp_server_broadcast_except(except_client, channel, data, bytes, flags)
- [ ] 2.8 Tests: test_buffer_api.cpp — acquire/write/send/release cycle, all write/read types round-trip, broadcast sends to all connected

## 3. Automatic Rekeying (P6-C) — spec 04 REQ-04.7
- [ ] 3.1 Implement rekey detection: check tx_nonce_counter >= REKEY_NONCE_THRESHOLD (2^30) or bytes_transmitted >= 1GB or epoch_duration >= 1h, whichever first
- [ ] 3.2 Implement REKEY flag: set bit 3 in prefix byte (type 0x0C = DATA_REKEY) on the triggering packet
- [ ] 3.3 Implement key derivation: HKDF-SHA256(old_key, "netudp-rekey" || epoch_number) for both tx and rx keys, reset nonce counters, reset replay protection, increment epoch_number
- [ ] 3.4 Implement grace window: sender accepts old-key packets for 256 more packets after rekey, then zeroes old keys
- [ ] 3.5 Tests: test_rekey.cpp — trigger rekey at threshold, verify new keys derived correctly, verify grace window accepts old-key packets, verify post-grace rejects old keys

## 4. Benchmark Suite (P6-D) — spec 14
- [ ] 4.1 Create bench/bench_main.cpp: benchmark runner with JSON + human-readable output, warm-up iterations, configurable runs
- [ ] 4.2 bench_pps.cpp: packets per second — loopback server+client, 64B encrypted reliable, measure steady-state PPS (target ≥ 2M)
- [ ] 4.3 bench_latency.cpp: end-to-end latency histogram — timestamps at send/recv, compute p50/p95/p99/max (target p99 ≤ 5µs loopback)
- [ ] 4.4 bench_simd_compare.cpp: side-by-side CRC32C/memcpy/ack_scan/replay_check for generic vs SSE4.2 vs AVX2 (target ≥ 20% SIMD improvement)
- [ ] 4.5 bench_scalability.cpp: PPS vs connection count (1, 10, 100, 1000 connections), plot throughput curve
- [ ] 4.6 bench_memory.cpp: RSS measurement with 1024 connections, no compression (target ≤ 100MB)
- [ ] 4.7 CI benchmark regression: GitHub Actions job that runs bench_pps + bench_latency, compare against stored baseline, fail if regression > 5%

## 5. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 5.1 Update CHANGELOG.md: "Added: network simulator, packet handler API, buffer acquire/send, broadcast, auto-rekeying, benchmark suite with CI regression"
- [ ] 5.2 All tests pass + all benchmarks run without crashes
- [ ] 5.3 Run clang-tidy — zero warnings
