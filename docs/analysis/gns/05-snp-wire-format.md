# 5. SNP Wire Format — The Reliability Protocol

## Overview

The SNP (Steam Network Protocol) is the internal reliability/framing layer. It is transport-agnostic — it produces a payload that gets encrypted and sent over UDP (or SDR relay).

The payload is a **sequence of frames**, each beginning with an 8-bit type/flags field. This is fundamentally different from netcode.io (one message per packet) or Server1 (messages prefixed with type bytes).

## Frame Types

```
00xxxxxx  Unreliable message segment
010xxxxx  Reliable message segment (stream-based)
100000xx  Stop waiting
1001xxxx  Ack frame
10001xxx  Select lane
100001xx  Reserved
101xxxxx  Reserved
11xxxxxx  Reserved
```

## Unreliable Message Segment

```
00emosss [message_num] [offset] [size] data

e: 1 = last segment of this message
m: message number encoding (relative or absolute)
o: offset within message (0 = first segment)
sss: data size encoding
```

Key insight: Unreliable messages ARE segmented (fragmented) — unlike netcode.io which has no fragmentation. Each segment is independently encoded with message number and offset, allowing any ordering of segments.

## Reliable Message Segment (Stream-Based)

```
010mmsss [stream_pos] [size] data

mm: stream position encoding (24/32/48-bit absolute, or 8/16/32-bit relative)
sss: data size encoding
```

**Reliable data is a byte stream** (like TCP), not individual messages. Message boundaries are encoded separately within the stream using a framing header:

```
0mssssss [msg_num] [msg_size]

m: message number (0 = previous+1, 1 = varint delta)
ssssss: message size (0-31 = inline, else varint)
```

## Ack Frame (DCCP/QUIC-Style Ack Vectors)

```
1001wnnn latest_received_pkt_num latest_received_delay [N] [ack_blocks...]
```

Each ack block:
```
aaaannnn [num_ack] [num_nack]

aaaa: count of consecutive ACKed packets
nnnn: count of consecutive NACKed packets
```

This is **run-length encoded ack/nack status** for every packet between `stop_waiting` and `latest_received`. The sender knows the exact fate of every packet it sent.

## Stop Waiting Frame

```
100000ww pkt_num_offset
```

Tells sender: "I've received all packets older than `this_pkt - offset - 1`, you can stop tracking them."

## Select Lane Frame

```
10001nnn [lane_number]
```

Changes the active lane for subsequent segment decoding. Default lane is 0.

## Key Design Insights

1. **Multiple frames per packet** — a single UDP packet contains multiple frame types (acks + unreliable data + reliable data + stop_waiting). This is the ultimate batching.

2. **Stream-based reliability** — reliable data is a continuous byte stream, not individual messages. The sender can retransmit different segment boundaries than originally sent. This is more flexible than per-packet retransmission.

3. **Ack vectors vs ack bitmask** — GNS sends the complete ack/nack state for ALL packets in the window, using RLE compression. This gives the sender perfect knowledge, enabling optimal retransmission decisions.

4. **Variable-length everything** — message numbers, stream positions, sizes, ack counts — all use the minimum bytes needed. Extremely bandwidth-efficient.

5. **Latest ack delay = RTT measurement** — every ack frame includes the delay between receiving the packet and sending the ack, giving continuous RTT measurements without separate ping/pong.
