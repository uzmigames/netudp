# Proposal: Reliability + Channels — Dual-Layer Ack, RTT, 4 Channel Types

## Why
Game networking requires both unreliable (position updates, voice) and reliable (chat, inventory, RPC) delivery. The dual-layer reliability engine (packet-level ack bitmask + per-channel message sequencing) provides this without separate ping/pong packets — RTT is measured from piggybacked ack delay fields. Four channel types (unreliable, unreliable sequenced, reliable ordered, reliable unordered) cover all game server use cases. Priority + weight scheduling prevents high-frequency unreliable data from starving reliable messages. Nagle batching reduces packet overhead.

## What Changes
- Packet sequence numbering (uint16) with piggybacked ack + 32-bit ack_bits + ack_delay_us
- Sequence window protection (33 packets, aligned with ack_bits coverage)
- RTT estimation: SRTT, RTTVAR, RTO (RFC 6298 adapted)
- 4 channel types with per-channel config (priority, weight, nagle timer)
- Per-channel reliable sequencing with 512-message send/recv buffers
- Retransmission: RTT-adaptive, exponential backoff (cap 2^5), max 10 retries
- Reliable ordered reorder buffer, reliable unordered immediate delivery with dup detection
- Unreliable sequenced (drop stale), stop-waiting optimization, keepalive packets

## Impact
- Affected specs: 06-channels, 07-reliability
- Affected code: src/channel/, src/reliability/
- Breaking change: NO
- User benefit: Game-ready reliability with 4 channel types and automatic RTT measurement
