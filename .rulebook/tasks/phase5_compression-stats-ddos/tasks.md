## 1. Bandwidth Control (P5-A) — spec 10
- [ ] 1.1 Create src/bandwidth/token_bucket.h/.cpp: per-connection TokenBucket (rate_bytes_per_sec default 256KB/s, burst_bytes default 32KB, tokens double, last_refill_us), try_consume(bytes) returns bool, refill(now_us) with min(tokens+rate*dt, burst)
- [ ] 1.2 Create src/bandwidth/budget.h/.cpp: per-connection QueuedBits (int32 queued_bits = 0), budget_refill(delta_time) subtracts allowed bits clamped to -burst, budget_consume(packet_bytes) adds bits, can_send() = queued_bits <= 0
- [ ] 1.3 Implement AIMD congestion control: track loss_rate from ack_bits over sliding window of last 64 packets, if loss > 5% → multiplicative decrease (×0.75, floor MIN_SEND_RATE 32KB/s), if loss < 1% + 10 RTT samples → additive increase (×1.10, cap max_send_rate), evaluate every RTT interval
- [ ] 1.4 Tests: test_bandwidth.cpp — token bucket refill/consume, QueuedBits defers when over budget, AIMD reduces on loss and increases on good conditions

## 2. Compression (P5-B) — spec 11
- [ ] 2.1 Integrate netc library: CMake FetchContent or vendored subdir, link netc::netc to netudp target
- [ ] 2.2 Create src/compress/compressor.h/.cpp: per-channel compressor — stateful mode (reliable ordered: context preserved across messages), stateless mode (unreliable: fresh context per message), passthrough guarantee (never expand payload — if compressed > original, send uncompressed with flag bit)
- [ ] 2.3 Implement dictionary loading: netc_dict_load() from file path in config, validate CRC32, store in server/client for lifetime
- [ ] 2.4 Wire into send pipeline: compress BEFORE encrypt (plaintext → compressed → AEAD encrypt), add 1-bit compression flag to frame header indicating compressed/raw
- [ ] 2.5 Wire into recv pipeline: AEAD decrypt → check compression flag → decompress if set → deliver to channel
- [ ] 2.6 Tests: test_compression.cpp — stateful compress/decompress round-trip, stateless round-trip, passthrough when incompressible, dictionary loading

## 3. DDoS Escalation (P5-C) — spec 05 REQ-05.7
- [ ] 3.1 Create src/connection/ddos.h/.cpp: DDoSMonitor struct — severity enum (None=0..Critical=4), bad_packets_per_sec counter (reset every second), cooloff_timer double
- [ ] 3.2 Implement escalation logic: on_bad_packet() increments counter, update(dt) evaluates thresholds (>100→Low, >500→Medium, >2000→High, >10000→Critical), should_process_new_connection() returns false at Critical, should_process_packet(is_established) returns false for non-established at High+
- [ ] 3.3 Implement auto-cooloff: after 30s (60s for Critical) without escalation trigger, reduce severity by one level
- [ ] 3.4 Tests: test_ddos.cpp — escalation through all levels, auto-cooloff, established connections unaffected at High

## 4. Statistics (P5-D) — spec 12
- [ ] 4.1 Create src/stats/stats.h: netudp_connection_stats_t (30+ fields per spec 12 REQ-12.1), netudp_channel_stats_t (9 fields per REQ-12.2), netudp_server_stats_t (11 fields per REQ-12.3)
- [ ] 4.2 Wire stats accumulation into all subsystems: packet_tracker → packets_sent/received/lost/out_of_order, reliability → messages_sent/received/dropped/window_stalls, fragment → fragments_sent/received/retransmitted/timed_out, crypto → replay_attacks_blocked/decrypt_failures, bandwidth → send_rate/queue_time, compression → ratio/bytes_saved
- [ ] 4.3 Implement throughput EMA: every 1-second window, out_bytes_per_sec = 0.8 × old + 0.2 × this_second, same for in/out packets/bytes
- [ ] 4.4 Implement API: netudp_server_connection_status(), netudp_server_channel_status(), netudp_server_stats() — all O(1) copy of accumulated struct
- [ ] 4.5 Tests: test_stats.cpp — verify counters increment correctly after known operations, throughput EMA converges, all API calls return valid data

## 5. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 5.1 Update CHANGELOG.md: "Added: AIMD congestion control, netc compression, DDoS 5-level escalation, GNS-level connection stats"
- [ ] 5.2 All tests pass: test_bandwidth, test_compression, test_ddos, test_stats
- [ ] 5.3 Run clang-tidy — zero warnings on all new files
