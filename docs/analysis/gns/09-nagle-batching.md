# 9. Nagle Algorithm & Batching

## How GNS Batching Works

By default, all messages are subject to Nagle's algorithm:
- Small messages are held briefly (typically ~200us-5ms configurable)
- When enough data accumulates to fill a packet, or the timer expires, the packet is sent
- `NoNagle` flag bypasses this for latency-critical messages
- `NoDelay` bypasses Nagle AND sends immediately (even if there's room for more)

## SNP Frame Packing

A single UDP packet can contain:
- An ack frame (state of received packets)
- A stop-waiting frame (advance sender's window)
- One or more unreliable message segments
- One or more reliable stream segments
- Lane-select frames to switch between lanes

All packed tightly with variable-length encoding. One 1200-byte UDP packet can carry the equivalent of 10-20 small game messages.

## Comparison with Server1 Batching

| Aspect | Server1 (Begin/End) | GNS (Nagle + SNP frames) |
|---|---|---|
| Batching trigger | Explicit API (BeginReliable/EndReliable) | Automatic (Nagle timer) |
| Control | Application controls | Library controls (with per-message flags) |
| Frame types | Single type per packet | Multiple frame types mixed |
| Efficiency | Good | Excellent (acks + data in same packet) |

## Relevance to netudp

netudp should combine both approaches:
1. **Automatic Nagle** — default batching with configurable timer
2. **Explicit flush** — `netudp_flush()` for immediate send
3. **NoNagle flag** — per-send bypass like GNS
4. **Mixed frames** — acks + data in the same packet (like GNS SNP)
