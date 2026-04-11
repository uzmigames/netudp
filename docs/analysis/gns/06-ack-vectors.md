# 6. Ack Vector System (DCCP/QUIC-Style)

## How It Works

Every ack frame contains:
1. `latest_received_pkt_num` — highest packet number received
2. `latest_received_delay` — microseconds since receiving that packet (16-bit, 32us precision)
3. RLE-encoded ack/nack blocks — complete state for ALL packets in window

## Ack Block Encoding

```
aaaannnn [num_ack] [num_nack]

Working backwards from latest_received:
  aaaa = consecutive ACKed packets (received)
  nnnn = consecutive NACKed packets (not yet received)
```

Example: packets 100-106, where 100,101,103,104,106 received, 102,105 lost:
```
latest_received = 106
Block 0: ack=1, nack=1   (106 acked, 105 nacked)
Block 1: ack=2, nack=1   (104,103 acked, 102 nacked)
Block 2: ack=2, nack=0   (101,100 acked)
```

## Why Better Than Simple Bitmask

| Approach | Info per ack | Bandwidth | Sender knowledge |
|---|---|---|---|
| Single ACK number | 1 packet | 2-8 bytes | Cumulative only |
| 32-bit bitmask (netcode.io style) | 33 packets | 6-10 bytes | 33-packet window |
| Ack vector (GNS) | **ALL packets** in window | Variable (RLE) | **Complete** |

With ack vectors, the sender knows the **exact** fate of every packet. This enables:
- Retransmit ONLY truly lost segments (not everything after a gap)
- Accurate loss rate measurement for congestion control
- Continuous RTT measurement from ack delay field

## Built-in RTT Measurement

```
latest_received_delay = time since receiving latest_received_pkt_num

RTT = (now - send_time_of_that_packet) - latest_received_delay
```

No separate ping/pong needed. Every ack frame is an implicit RTT sample.

## Relevance to netudp

netudp should adopt a simplified version:
- 32-bit ack bitmask for the common case (like Server1/netcode.io)
- Optional extended ack vectors for high-loss scenarios
- Always include ack delay for RTT measurement
