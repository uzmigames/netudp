# Spec 06 — Channel System

## Requirements

### REQ-06.1: Channel Types

```cpp
enum class ChannelType : uint8_t {
    Unreliable         = 0,  // Fire-and-forget. No ordering. No retransmit.
    UnreliableSequenced= 1,  // Drop stale (older sequence). No retransmit.
    ReliableOrdered    = 2,  // Guaranteed delivery. Strict ordering. Retransmit.
    ReliableUnordered  = 3,  // Guaranteed delivery. Any order. Retransmit.
};
```

### REQ-06.2: Channel Configuration

```cpp
struct ChannelConfig {
    ChannelType type;
    int         priority;        // Lower = higher priority (strict between priorities)
    uint16_t    weight;          // Weight within same priority (fair queueing)
    uint32_t    nagle_time_us;   // Nagle timer. 0 = disabled (send immediately).
    uint32_t    max_message_size;// Per-channel max. 0 = MTU only, no fragmentation.
};
```

Default channels:
```
Ch 0: ReliableOrdered,      priority=0, weight=1, nagle=5000µs  (control/RPCs)
Ch 1: Unreliable,           priority=1, weight=3, nagle=0        (game state)
Ch 2: ReliableUnordered,    priority=1, weight=1, nagle=5000µs   (events/chat)
Ch 3: UnreliableSequenced,  priority=2, weight=1, nagle=0        (latest-state)
```

### REQ-06.3: Maximum Channels
Up to 255 channels per connection (uint8_t index). Default: 4.

### REQ-06.4: Priority Scheduling

When multiple channels have pending data and bandwidth is limited:
1. Send ALL pending data from highest priority (lowest number) first
2. Within same priority: weighted fair queue (e.g., weight 3 gets 3/4 bandwidth)
3. Lower priority only gets bandwidth when higher priorities are empty

### REQ-06.5: Nagle Algorithm

When `nagle_time_us > 0`:
1. First message starts the Nagle timer
2. Subsequent messages within timer period are batched into the same packet
3. When timer expires OR accumulated size >= MTU: flush
4. `NETUDP_SEND_NO_NAGLE` flag bypasses the timer for that specific message
5. `NETUDP_SEND_NO_DELAY` bypasses Nagle AND sends immediately
6. `netudp_flush()` forces immediate send of all pending data

### REQ-06.6: Per-Channel Compression

If compression enabled:
- `ReliableOrdered`: netc **stateful** context (delta + adaptive)
- `ReliableUnordered`: netc **stateless** (no cross-packet dependency)
- `Unreliable`: netc **stateless**
- `UnreliableSequenced`: netc **stateless**

One stateful `netc_ctx_t` per reliable ordered channel per connection (pre-allocated).

### REQ-06.7: Per-Channel Statistics

```cpp
struct ChannelStats {
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t messages_dropped;      // Unreliable dropped, reliable retransmit exhausted
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t pending_bytes;         // Queued but not yet sent
    uint32_t unacked_reliable;      // Sent but not acked
    uint64_t queue_time_us;         // Estimated wait time in queue
    float    compression_ratio;     // 0..1 (0 = no compression)
};
```

## Scenarios

#### Scenario: Priority scheduling
Given channel 0 (priority=0) has 500 bytes pending
And channel 1 (priority=1) has 2000 bytes pending
And bandwidth allows 1000 bytes this tick
When scheduler runs
Then channel 0's 500 bytes are sent first
Then 500 bytes from channel 1 are sent
And 1500 bytes from channel 1 remain queued

#### Scenario: Nagle batching
Given channel 0 has nagle_time_us = 5000 (5ms)
When app sends 3 small messages (10B each) within 2ms
Then all 3 are packed into 1 UDP packet after 5ms timer expires

#### Scenario: NoNagle bypass
Given channel 0 has nagle_time_us = 5000
When app sends with flag NETUDP_SEND_NO_NAGLE
Then message is included in the next packet without waiting for timer

#### Scenario: Unreliable sequenced drop stale
Given latest received sequence on channel 3 is 50
When packet with sequence 45 arrives
Then message is silently dropped (stale)
When packet with sequence 51 arrives
Then message is delivered to application
