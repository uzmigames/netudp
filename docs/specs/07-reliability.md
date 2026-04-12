# Spec 07 — Dual-Layer Reliability Engine

## Requirements

### REQ-07.1: Layer 1 — Packet-Level Acknowledgment

Every outgoing data packet SHALL include:

**Clear header (unencrypted, see spec 09 REQ-09.2):**
- `sequence` (1-8 bytes, variable-length): this packet's sequence (wraps at 65535)

**Ack fields (inside encrypted payload, 8 bytes):**
```cpp
struct AckFields {
    uint16_t ack;            // Highest received sequence from remote
    uint32_t ack_bits;       // Bitmask: bit N = received (ack - N - 1)
    uint16_t ack_delay_us;   // Microseconds since receiving 'ack'
};
// Total: 8 bytes (inside encrypted payload, first bytes after decryption)
```

**Note:** The `sequence` field is NOT inside the encrypted payload — it is in the clear header (spec 09 REQ-09.2) and is authenticated indirectly via the AEAD nonce (spec 04 REQ-04.3). Only the ack fields (8 bytes) are inside the encrypted payload.

- `sequence` SHALL increment monotonically per connection per direction
- `ack` SHALL be the highest packet sequence received from the remote
- `ack_bits` SHALL be a 32-bit bitmask where bit N indicates packet `(ack - N - 1)` was received
- `ack_delay_us` SHALL be the time in microseconds since receiving the packet numbered `ack`

### REQ-07.2: Sequence Window Protection

The sender SHALL NOT send if `sequence - oldest_unacked >= 33`.
This aligns the send window with the `ack_bits` coverage: the `ack` field plus 32 bits covers exactly 33 packets. Packets beyond this range are invisible to the receiver's ack bitmask and cannot be acknowledged.

When the window is full:
1. Only keepalive packets (empty, with ack fields) are sent
2. Application send calls return `NETUDP_ERROR_WINDOW_FULL`
3. Stats counter `window_stalls` is incremented

**Rationale:** The previous window of 256 was incompatible with the 32-bit `ack_bits` field, which can only report on 33 packets (ack + 32 prior). A 256-packet window would leave 223 packets unacknowledgeable. The window of 33 matches the ack coverage exactly. If higher throughput is needed, `ack_bits` can be extended to `uint64_t` (64-bit, covering 65 packets) as a future enhancement.

### REQ-07.3: Layer 2 — Per-Channel Message Reliability

Each reliable channel SHALL maintain:

```cpp
struct SentMessage {
    uint16_t sequence;
    uint16_t packet_sequence;  // Which packet carried this message
    double   send_time;
    int      retry_count;
    int      data_offset;      // Offset into send pool
    int      data_len;
};

struct ReceivedMessage {
    uint16_t sequence;
    int      data_offset;      // Offset into recv pool
    int      data_len;
    bool     valid;            // Slot occupied
};

struct ChannelReliabilityState {
    uint16_t send_seq;           // Next message sequence to assign
    uint16_t recv_seq;           // Next expected from remote
    uint16_t oldest_unacked;     // Oldest unacked sent message

    // Sent messages awaiting ack
    FixedRingBuffer<SentMessage, 512> sent_buffer;

    // Out-of-order received messages (reliable ordered only)
    FixedRingBuffer<ReceivedMessage, 512> recv_buffer;
};
```

**Receive path by channel type:**

- **Reliable Ordered:** Messages received out-of-order are stored in `recv_buffer` by sequence. Messages are delivered to the application only when `recv_seq` is contiguous (i.e., `recv_buffer[recv_seq]` is valid). Delivery advances `recv_seq` and delivers consecutive buffered messages.

- **Reliable Unordered:** Messages are delivered to the application **immediately** upon receipt (no reorder buffer needed). The `recv_buffer` is NOT used. Instead, a 512-bit bitmask tracks which sequences have been received to detect and drop duplicates. `recv_seq` tracks the oldest undelivered sequence for bitmask window advancement.

When a packet is NACKed (not in ack_bits):
1. Determine which messages were in that packet (via `packet_sequence` mapping)
2. Re-queue those messages for retransmission in the next outgoing packet
3. Increment `retry_count` for each

### REQ-07.4: RTT Estimation

```
sample_rtt = (now - send_time_of_packet[ack]) - ack_delay_us
srtt       = 0.875 × srtt + 0.125 × sample_rtt
rttvar     = 0.75 × rttvar + 0.25 × |srtt - sample_rtt|
rto        = max(100ms, min(2000ms, srtt + 4 × rttvar))
```

Initial values: `srtt = 0`, `rttvar = 0`, `rto = 1000ms`.
First sample: `srtt = sample`, `rttvar = sample / 2`.

### REQ-07.5: Retransmission Strategy

- Retransmit when `now - send_time > rto`
- Exponential backoff: `rto_effective = rto × 2^min(retry_count, 5)`
- Max retries: 10. After that: message dropped, `stats.messages_dropped++`
- Retransmitted messages are embedded in the next regular outgoing packet (not a separate retransmit packet)

**Timeline cross-reference:** With `rto` maxed at 2000ms and exponent capped at 5, the max effective RTO is 64s. At 10 max retries, the worst-case total retransmit duration is ~127s (sum of geometric series). The connection timeout (spec 05, default 10s) and keepalive interval (1000ms, REQ-07.7) will detect a dead connection well before the 10th retry. In practice, a connection timeout fires after 10s of no acks, which caps the effective retries to ~5-6 at typical RTOs.

### REQ-07.6: Stop-Waiting

Periodically (every 32 packets or when ack window > 50% full), include a stop-waiting frame:

```
"I have received all packets before sequence X"
```

This allows the remote sender to release tracking state for old packets.

### REQ-07.7: Keepalive

If no data packet has been sent within `keepalive_interval` (default: 1000ms):
1. Send an empty packet with only the PacketHeader (ack info)
2. This advances the ack window and provides RTT measurement

### REQ-07.8: Replay Protection

Replay protection is defined in spec 04 REQ-04.9. It operates on the 64-bit nonce counter (not the 16-bit wire sequence). See spec 04 for the `ReplayProtection` struct, rejection rules, and wraparound safety guarantees.

Applied to all encrypted packets BEFORE decryption attempt. The 64-bit nonce counter is derived from the packet's position in the key epoch (mapped from the wire sequence + epoch state).

## Scenarios

#### Scenario: Normal ack flow
Given client sends packet seq=10
When server receives it
Then server's next packet includes ack=10, ack_delay_us=<measured>
When client receives that ack
Then client marks seq=10 as delivered
And calculates RTT from ack_delay

#### Scenario: Packet loss detection
Given client sends packets seq=10,11,12
And packet 11 is lost
When server sends ack=12, ack_bits=0b101 (received 10 and 12, not 11)
Then client determines packet 11 was lost
And retransmits messages that were in packet 11

#### Scenario: Reliable ordered delivery under loss
Given server sends reliable messages A(seq=1), B(seq=2), C(seq=3)
And message B is lost
When client receives A and C
Then client delivers A immediately
And buffers C in recv_buffer
When B is retransmitted and received
Then client delivers B then C (in order)

#### Scenario: Window full stall
Given 256 packets sent without any ack received
When app calls send()
Then returns NETUDP_ERROR_WINDOW_FULL
When keepalive with ack arrives
Then window advances, sends resume

#### Scenario: Max retries exhausted
Given reliable message retransmitted 10 times without ack
When 11th retry would be attempted
Then message is dropped
And stats.messages_dropped incremented
And connection quality reduced
