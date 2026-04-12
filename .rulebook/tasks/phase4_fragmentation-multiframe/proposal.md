# Proposal: Fragmentation + Multi-Frame — Large Messages + Efficient Wire Format

## Why
Games need to send messages larger than the MTU (1200 bytes) — entity snapshots, terrain chunks, asset data. Transparent fragmentation with per-fragment retransmission (only lost fragments, not the whole message) is a significant improvement over UE5's all-or-nothing retransmit. Multi-frame packets (ack + stop-waiting + data in one UDP packet) maximize bandwidth efficiency by eliminating dedicated ack-only packets in most cases. The variable-length sequence encoding and frame type system complete the wire format.

## What Changes
- Fragment header (message_id, fragment_index, fragment_count) + splitting at MTU boundary
- Fragment bitmask tracking with SIMD popcount for missing fragment detection
- Reassembly buffer (pre-allocated per connection, per-channel max_message_size sizing, 5s timeout)
- Fragment-level retransmission (only missing fragments)
- Multi-frame packet assembly: AckFields(8B) + STOP_WAITING + UNRELIABLE_DATA/RELIABLE_DATA/FRAGMENT_DATA frames
- Frame type encoding/decoding (0x02-0x06)
- Variable-length sequence encoding in prefix byte (1-8 bytes)
- Full send pipeline: app → channel → fragment → encrypt → batch → socket
- Full recv pipeline: socket → decrypt → defragment → channel → app

## Impact
- Affected specs: 08-fragmentation, 09-wire-format
- Affected code: src/fragment/, src/wire/, src/server.cpp, src/client.cpp (pipeline integration)
- Breaking change: NO
- User benefit: Transparent large message support up to 288KB with efficient wire format
