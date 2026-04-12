# 06. Frame Coalescing

**Status:** NOT IMPLEMENTED — critical missing optimization.

## Problem

The current `server_send_pending()` in `src/server.cpp:847–937` sends **one message per UDP packet**:

```cpp
while (ch_idx >= 0) {
    dequeue_send(&qmsg);                    // 1 message
    write_ack_fields(...);                   // ack header
    write_reliable_frame(...)                // 1 frame
        OR write_unreliable_frame(...);
    packet_encrypt(...);                     // 1 encrypt
    socket_send(...);                        // 1 syscall  ← THE PROBLEM
    ch_idx = next_channel(...);
}
```

The wire format already supports multiple frames per payload — `frame.h` defines frames with type + length, and the receive parser iterates `peek_frame_type()` until the buffer is exhausted. But the send side never packs more than one frame per packet.

## Cost of Not Coalescing

### Per-packet overhead

```
Wire overhead per UDP packet:
  prefix_byte:     1 byte
  nonce_counter:   8 bytes
  MAC (Poly1305): 16 bytes
  AckFields:      ~8 bytes
  ─────────────────────────
  Total:          33 bytes fixed overhead per packet
```

### 5 small messages (20 bytes each = 100 bytes of actual data)

```
WITHOUT coalescing (5 separate UDP packets):

  Pkt 1: [prefix 1][nonce 8][ack 8][frame_hdr 4][data 20][MAC 16] = 57 bytes
  Pkt 2: [prefix 1][nonce 8][ack 8][frame_hdr 4][data 20][MAC 16] = 57 bytes
  Pkt 3: [prefix 1][nonce 8][ack 8][frame_hdr 4][data 20][MAC 16] = 57 bytes
  Pkt 4: [prefix 1][nonce 8][ack 8][frame_hdr 4][data 20][MAC 16] = 57 bytes
  Pkt 5: [prefix 1][nonce 8][ack 8][frame_hdr 4][data 20][MAC 16] = 57 bytes

  Wire total: 285 bytes  (data efficiency: 35%)
  Syscalls:   5 × 7,231ns = 36,155ns
  Encrypts:   5 × 948ns   =  4,740ns
  Total cost: ~40,895ns for 100 bytes of data

WITH coalescing (1 UDP packet, 5 frames):

  Pkt 1: [prefix 1][nonce 8][ack 8]
         [frame_hdr 4][data 20]
         [frame_hdr 4][data 20]
         [frame_hdr 4][data 20]
         [frame_hdr 4][data 20]
         [frame_hdr 4][data 20]
         [MAC 16]                              = 153 bytes

  Wire total: 153 bytes  (data efficiency: 65%)
  Syscalls:   1 × 7,231ns =  7,231ns
  Encrypts:   1 × 948ns   =    948ns
  Total cost: ~8,179ns for 100 bytes of data
```

**Result: 5× less time, 46% less bandwidth, same 100 bytes delivered.**

### MMORPG scenario (1,000 players × 5 updates × 20 Hz)

```
WITHOUT coalescing:
  100,000 sendto/s × 7.2µs = 720ms/s syscall time  (72% of 1 CPU core!)
  100,000 encrypt/s × 948ns = 95ms/s crypto time
  Total: 815ms/s → IMPOSSIBLE on a single core

WITH coalescing (~5 msgs/packet):
  20,000 sendto/s × 7.2µs = 144ms/s  (14% of 1 core)
  20,000 encrypt/s × 948ns = 19ms/s
  Total: 163ms/s → Comfortable headroom
```

## How It Should Work

### New send loop (pseudocode)

```cpp
void server_send_pending(netudp_server* server, int slot) {
    Connection& conn = server->connections[slot];
    double now = server->current_time;

    if (!conn.budget.can_send()) return;

    // 1. Start a payload buffer
    uint8_t payload[NETUDP_MTU];
    int payload_pos = 0;

    // 2. Write AckFields ONCE
    AckFields ack = conn.packet_tracker.build_ack_fields(now);
    payload_pos += write_ack_fields(ack, payload + payload_pos);

    int frames_packed = 0;

    // 3. Pack frames from ALL channels until MTU is full
    int ch_idx = ChannelScheduler::next_channel(channels, num_channels, now);
    while (ch_idx >= 0) {
        QueuedMessage qmsg;
        if (!conn.ch(ch_idx).dequeue_send(&qmsg)) {
            ch_idx = ChannelScheduler::next_channel(...);
            continue;
        }

        // Check if this frame fits in remaining space
        int frame_overhead = (is_reliable ? 6 : 4);
        int needed = frame_overhead + qmsg.size;
        int remaining = NETUDP_MTU - payload_pos - 16; // reserve MAC

        if (needed > remaining && frames_packed > 0) {
            // Flush current packet, start new one
            flush_packet(server, conn, payload, payload_pos);
            payload_pos = 0;
            payload_pos += write_ack_fields(ack, payload);
            frames_packed = 0;
        }

        // Write frame
        payload_pos += write_frame(..., payload + payload_pos, ...);
        frames_packed++;

        ch_idx = ChannelScheduler::next_channel(...);
    }

    // 4. Flush remaining frames
    if (frames_packed > 0) {
        flush_packet(server, conn, payload, payload_pos);
    }
}
```

### Flush behavior

Two triggers for flushing a coalesced packet:
1. **MTU full** — no more room for another frame (hard limit)
2. **No more pending messages** — all channels drained (natural flush)

The Nagle timer already provides delay-based batching at the channel level. Frame coalescing adds **space-based batching** at the packet level.

## Comparison with Legacy Servers

| Aspect | Server1 (C# UDP) | MMORPG TS (WebSocket) | netudp (current) | netudp (proposed) |
|--------|------------------|----------------------|-------------------|-------------------|
| Batching trigger | MTU limit (1200B) | 512KB or timer tick | None | MTU limit |
| Delimiter | Implicit (type+seq) | Explicit `0xFEFEFEFE` | N/A | Implicit (frame type+len) |
| Flush | Every game tick | Every game tick | Every dequeue | MTU full or queue empty |
| Overhead per sub-msg | 0 bytes | 4 bytes | 33 bytes (!) | 4–6 bytes (frame header only) |
| Dedup | None | Hex comparison | None | None (not needed at transport) |

## Wire Format Compatibility

The wire format already supports this — no protocol change needed:

```
Encrypted payload (current):
  [AckFields][1 frame]

Encrypted payload (with coalescing):
  [AckFields][frame1][frame2][frame3]...[frameN]

Each frame is self-delimiting:
  UNRELIABLE: type(1) + channel(1) + len(2) + data(len)  = 4 + N
  RELIABLE:   type(1) + channel(1) + seq(2) + len(2) + data(len) = 6 + N
  FRAGMENT:   type(1) + channel(1) + msg_id(2) + idx(1) + cnt(1) + data(len) = 6 + N
```

The receive side already iterates frames via `peek_frame_type()` — it will parse multi-frame packets correctly with zero changes.

## Implementation Effort

| Component | Change needed |
|-----------|--------------|
| `server_send_pending()` | Rewrite send loop (accumulate frames, flush on MTU/empty) |
| `frame.h` | No change (already self-delimiting) |
| Receive parser | No change (already iterates frames) |
| `netudp_buffer` API | No change |
| Channel queue | No change |
| Tests | Add multi-frame pack/unpack tests |
| Benchmark | Measure PPS before/after |

**Estimated effort: 1 day implementation, 1 day testing.**
