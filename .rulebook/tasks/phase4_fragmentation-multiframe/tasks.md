## 1. Fragmentation (P4-A) — spec 08
- [ ] 1.1 Create src/fragment/fragment.h: FragmentHeader struct (message_id uint16, fragment_index uint8, fragment_count uint8 — 4 bytes), FragmentTracker struct (message_id, total_fragments, received_count, received_mask[32], first_recv_time, last_recv_time, buffer_offset, buffer_capacity), constants MAX_FRAGMENT_COUNT=255, DEFAULT_MAX_MESSAGE_SIZE=64KB, ABSOLUTE_MAX_MESSAGE_SIZE=288KB
- [ ] 1.2 Implement message splitting: calculate fragment_count = ceil(msg_size / max_fragment_payload), max_fragment_payload = MTU - clear_header - fragment_header(4B) - AEAD_tag(16B), assign message_id from per-connection counter, split into fragment_count fragments, validate fragment_count in [1,255]
- [ ] 1.3 Implement fragment bitmask tracking: set bit in received_mask[32] on fragment receipt, is_complete() = received_count == total_fragments, next_missing() using g_simd->fragment_next_missing (SIMD tzcnt on inverted mask)
- [ ] 1.4 Implement reassembly buffer: FixedRingBuffer<FragmentTracker, 16> active_reassemblies per connection, Pool element size = channel's max_message_size (not ABSOLUTE_MAX), acquire from pool on first fragment, copy fragment data at correct offset, deliver complete message to channel layer
- [ ] 1.5 Implement fragment-level retransmission: track which fragments were carried in which packet (via packet_sequence), on NACK only retransmit missing fragments (not whole message), follow same RTT-adaptive backoff as regular messages
- [ ] 1.6 Implement fragment timeout cleanup: in update(), scan active_reassemblies, if now - first_recv_time > fragment_timeout (5000ms), release buffer to pool, discard tracker, increment stats.fragments_timed_out
- [ ] 1.7 Tests: test_fragment.cpp — split 64KB message into 57 fragments, reassemble in order, reassemble out-of-order, missing fragment detection, timeout cleanup, max message size validation rejects >288KB

## 2. Multi-Frame Packet Assembly (P4-B) — spec 09
- [ ] 2.1 Create src/wire/frame.h: frame type constants (STOP_WAITING=0x02, UNRELIABLE_DATA=0x03, RELIABLE_DATA=0x04, FRAGMENT_DATA=0x05, DISCONNECT=0x06), frame encode/decode functions for each type
- [ ] 2.2 Create src/wire/packet_assembler.h/.cpp: PacketAssembler — builds outgoing packet: write prefix byte, variable-length sequence, connection_id(4B) as clear header; write AckFields(8B) as first encrypted bytes; pack frames from channel queues in priority order; stop when remaining < min frame size
- [ ] 2.3 Implement variable-length sequence encoding: count high zero bytes of sequence value, write seq_bytes count in prefix bits 7-4, write 1-8 bytes little-endian; decode: read seq_bytes from prefix, read that many bytes
- [ ] 2.4 Create src/wire/packet_parser.h/.cpp: PacketParser — read prefix byte, extract seq_bytes + packet_type, read sequence, read connection_id, decrypt payload, read AckFields(8B), iterate frames by type byte + decode each
- [ ] 2.5 Tests: test_wire.cpp — assemble multi-frame packet (ack + reliable + unreliable), parse back correctly, variable-length seq round-trip for values 0/255/65535/2^32/2^64-1, prefix byte encoding/decoding

## 3. Full Pipeline Integration (P4-C)
- [ ] 3.1 Implement full send pipeline in server/client update(): dequeue messages from channels → fragment if > MTU → assemble multi-frame packets → encrypt (AEAD with nonce counter) → batch into UDP datagrams → socket_send
- [ ] 3.2 Implement full recv pipeline: socket_recv → parse prefix → if handshake: handle unencrypted; if data: decrypt → parse AckFields → process ack/nack → iterate frames → defragment if FRAGMENT_DATA → deliver to channel → deliver to app
- [ ] 3.3 Integration test: test_pipeline.cpp — send 64KB reliable message through full pipeline, verify arrives intact; send 256KB message, verify fragmented + reassembled; send mix of reliable+unreliable across multiple channels simultaneously

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update CHANGELOG.md: "Added: message fragmentation (up to 288KB), multi-frame packets, variable-length sequence, full send/recv pipeline"
- [ ] 4.2 All tests pass: test_fragment, test_wire, test_pipeline (including large message tests)
- [ ] 4.3 Run clang-tidy on src/fragment/ and src/wire/ — zero warnings
