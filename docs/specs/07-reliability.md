# Spec 07 — Dual-Layer Reliability Engine

## Requirements

### REQ-07.1: Layer 1 — Packet-Level Acknowledgment

Every outgoing data packet SHALL include:

```cpp
struct PacketHeader {
    uint16_t sequence;       // This packet's sequence (wraps at 65535)
    uint16_t ack;            // Highest received sequence from remote
    uint32_t ack_bits;       // Bitmask: bit N = received (ack - N - 1)
    uint16_t ack_delay_us;   // Microseconds since receiving 'ack'
};
// Total: 10 bytes (inside encrypted payload)
```

- `sequence` SHALL increment monotonically per connection per direction
- `ack` SHALL be the highest packet sequence received from the remote
- `ack_bits` SHALL be a 32-bit bitmask where bit N indicates packet `(ack - N - 1)` was received
- `ack_delay_us` SHALL be the time in microseconds since receiving the packet numbered `ack`

### REQ-07.2: Sequence Window Protection

The sender SHALL NOT send if `sequence - oldest_unacked >= 256`.
This prevents overflowing the ack window. When the window is full:
1. Only keepalive packets (empty, with ack header) are sent
2. Application send calls return `NETUDP_ERROR_WINDOW_FULL`
3. Stats counter `window_stalls` is incremented

### REQ-07.3: Layer 2 — Per-Channel Message Reliability

Each reliable channel SHALL maintain:

```cpp
struct ChannelReliabilityState {
    uint16_t send_seq;           // Next message sequence to assign
    uint16_t recv_seq;           // Next expected from remote
    uint16_t oldest_unacked;     // Oldest unacked sent message

    // Sent messages awaiting ack
    FixedRingBuffer<SentMessage, 512> sent_buffer;

    // Out-of-order received messages (reliable ordered only)
    FixedRingBuffer<ReceivedMessage, 512> recv_buffer;
};

struct SentMessage {
    uint16_t sequence;
    uint16_t packet_sequence;  // Which packet carried this message
    double   send_time;
    int      retry_count;
    int      data_offset;      // Offset into send pool
    int      data_len;
};
```

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

```cpp
struct ReplayProtection {
    uint64_t most_recent;
    uint64_t received[256];

    bool already_received(uint64_t seq) const;
    void advance(uint64_t seq);
    void reset();
};
```

Applied to all encrypted packets BEFORE decryption attempt (sequence is in clear header).

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
