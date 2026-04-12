## 1. Packet-Level Ack (P3-A) — spec 07 REQ-07.1/07.2
- [ ] 1.1 Create src/reliability/packet_tracker.h/.cpp: PacketTracker struct — uint16 send_sequence (monotonic per direction), uint16 recv_ack (highest received), uint32 ack_bits, uint16 ack_delay_us, SentPacketRecord[33] circular buffer tracking send_time + acked flag
- [ ] 1.2 Implement AckFields serialization: write ack(2B) + ack_bits(4B) + ack_delay_us(2B) as first 8 bytes of encrypted payload, read on recv and process acks
- [ ] 1.3 Implement ack processing: on receiving packet, update recv_ack and ack_bits, compute ack_delay_us from time since receiving ack'd packet; on sending, include current ack state
- [ ] 1.4 Implement sequence window protection: sender blocks if send_sequence - oldest_unacked >= 33, return NETUDP_ERROR_WINDOW_FULL, increment stats.window_stalls
- [ ] 1.5 Tests: test_packet_tracker.cpp — sequential send/ack, out-of-order ack, window full detection, ack_bits correctly identifies missing packets

## 2. RTT Estimation (P3-B) — spec 07 REQ-07.4
- [ ] 2.1 Create src/reliability/rtt.h/.cpp: RttEstimator struct — srtt, rttvar, rto (initial 1000ms), first_sample flag
- [ ] 2.2 Implement RTT calculation: sample_rtt = (now - send_time_of_packet[ack]) - ack_delay_us, then SRTT/RTTVAR/RTO per RFC 6298 formula, clamp RTO to [100ms, 2000ms]
- [ ] 2.3 Tests: test_rtt.cpp — first sample sets srtt=sample, subsequent samples converge, RTO stays within bounds

## 3. Channel System (P3-C) — spec 06
- [ ] 3.1 Create src/channel/channel.h: ChannelType enum (UNRELIABLE=0, UNRELIABLE_SEQUENCED=1, RELIABLE_ORDERED=2, RELIABLE_UNORDERED=3), ChannelConfig struct (type, priority uint8, weight uint8, nagle_ms uint16, max_message_size)
- [ ] 3.2 Create src/channel/channel.cpp: Channel base with send queue, per-channel send_seq/recv_seq, config, stats counters
- [ ] 3.3 Implement Nagle timer: per-channel timer (default from config), accumulate messages until timer fires or NoNagle flag set, flush on timer expiry or explicit flush() call
- [ ] 3.4 Implement priority + weight scheduling: sort channels by priority (higher first), within same priority distribute by weight ratio, fill outgoing packet by dequeuing from highest-priority channels first
- [ ] 3.5 Tests: test_channel.cpp — config validation, Nagle batches messages, NoNagle bypasses timer, priority ordering correct

## 4. Dual-Layer Reliability (P3-D) — spec 07 REQ-07.3/07.5
- [ ] 4.1 Create src/reliability/reliable_channel.h/.cpp: ChannelReliabilityState — send_seq uint16, recv_seq uint16, oldest_unacked uint16, FixedRingBuffer<SentMessage, 512> sent_buffer, FixedRingBuffer<ReceivedMessage, 512> recv_buffer
- [ ] 4.2 Implement reliable send: assign per-channel send_seq, create SentMessage record (sequence, packet_sequence, send_time, retry_count=0, data), add to sent_buffer
- [ ] 4.3 Implement retransmission: on packet NACK (not in ack_bits), find SentMessage by packet_sequence mapping, re-queue for next packet, rto_effective = rto × 2^min(retry_count, 5), max 10 retries then drop + stats.messages_dropped++
- [ ] 4.4 Implement reliable ordered delivery: store out-of-order messages in recv_buffer by sequence, deliver to app only when recv_seq is contiguous (recv_buffer[recv_seq].valid), advance recv_seq and deliver consecutive buffered messages
- [ ] 4.5 Implement reliable unordered delivery: deliver immediately on receipt, use 512-bit bitmask to track received sequences for duplicate detection, advance window when oldest tracked sequence can be evicted
- [ ] 4.6 Implement unreliable sequenced: compare incoming msg_seq with last_delivered_seq, drop if <= (stale), deliver if > and update last_delivered_seq
- [ ] 4.7 Implement stop-waiting optimization: every 32 packets or when ack window > 50% full, include STOP_WAITING frame ("I received all before seq X"), remote sender releases tracking state for sequences < X
- [ ] 4.8 Implement keepalive: if no data packet sent within keepalive_interval (1000ms), send empty packet with only AckFields (8 bytes), advances ack window and provides RTT measurement
- [ ] 4.9 Tests: test_reliable_ordered.cpp — in-order delivery, out-of-order buffering, loss + retransmit + eventual delivery, max retries exhausted
- [ ] 4.10 Tests: test_reliable_unordered.cpp — immediate delivery regardless of order, duplicate rejection
- [ ] 4.11 Tests: test_unreliable_sequenced.cpp — stale packets dropped, newer delivered
- [ ] 4.12 Integration test: test_reliability_integration.cpp — mixed channel types under simulated 10% packet loss, verify all reliable messages delivered in correct order, unreliable_sequenced drops stale correctly

## 5. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 5.1 Update CHANGELOG.md: "Added: dual-layer reliability, 4 channel types, RTT estimation, Nagle batching, priority scheduling, stop-waiting"
- [ ] 5.2 All tests pass including integration test under simulated loss
- [ ] 5.3 Run clang-tidy on src/channel/ and src/reliability/ — zero warnings
