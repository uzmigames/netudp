# 3. Reliability: Dual-Layer (Packet + Bunch)

## Layer 1: Packet-Level (FNetPacketNotify)

- Each packet gets a 14-bit sequence number
- Receiver sends ack history (256-bit bitmask of received packets)
- Gap detection: if sequence doesn't increment by 1, intermediates marked lost
- Sender tracks `OutAckPacketId` (highest acked packet)

## Layer 2: Bunch-Level (per-channel)

- Reliable bunches get per-channel sequence numbers (max 1024 outstanding)
- Sent bunches stored in `OutRec` buffer per channel
- When packet containing reliable bunch is NAKed, **entire bunch is retransmitted**
- Unreliable bunches: no retransmission

## Partial Bunch Handling (for large data)

If a bunch exceeds max packet size:
1. Split into `bPartialInitial` → `bPartial`... → `bPartialFinal`
2. If **any** partial is lost, the **entire bunch must be resent**
3. Receiver queues partials and reassembles on `bPartialFinal`

**Lesson for netudp:** UE5's partial bunch retransmission is expensive — losing one fragment means resending all fragments. netudp should use fragment-level bitmask tracking (like we designed) so only lost fragments are retransmitted.

## Sequence Window Safety

```cpp
// Won't send if outstanding packets exceed window
if (NextOutSeq >= OutAckSeq + MaxSequenceHistoryLength)
    // Block sending until acks arrive
```

If 256 packets are sent without any ack arriving, the connection stalls. Keepalive packets (empty with ack history) help advance the window.

## What netudp Learns

1. **Dual-layer reliability is proven** — packet-level ack + per-channel message reliability
2. **Whole-bunch retransmit is wasteful** — use per-fragment retransmit instead
3. **Sequence window limit prevents overflow** — implicit backpressure, keep this
4. **Keepalive advances ack window** — empty packets with acks are critical
