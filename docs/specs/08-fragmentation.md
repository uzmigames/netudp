# Spec 08 — Fragmentation & Reassembly

## Requirements

### REQ-08.1: Fragment Header

```cpp
struct FragmentHeader {
    uint16_t message_id;       // Identifies the original message (wraps at 65535)
    uint8_t  fragment_index;   // This fragment's index (0-based)
    uint8_t  fragment_count;   // Total fragments in this message
};
// 4 bytes overhead per fragment
```

### REQ-08.2: Splitting

When `message_size > max_payload_per_packet`:
1. Calculate `fragment_count = ceil(message_size / max_fragment_payload)`
2. `max_fragment_payload = MTU - packet_header - fragment_header - AEAD_tag`
3. Assign `message_id` from per-connection counter (uint16, wrapping)
4. Split message into `fragment_count` fragments
5. Each fragment is sent as a separate DATA frame with FragmentHeader

### REQ-08.3: Maximum Message Size

```cpp
static constexpr int MAX_FRAGMENT_COUNT = 255;  // uint8_t fragment_index
static constexpr int DEFAULT_MAX_MESSAGE_SIZE = 64 * 1024;   // 64 KB
static constexpr int ABSOLUTE_MAX_MESSAGE_SIZE = 512 * 1024; // 512 KB
```

Configurable per channel via `ChannelConfig.max_message_size`.
Channels with `max_message_size = 0` do not support fragmentation (messages > MTU rejected).

### REQ-08.4: Fragment Bitmask Tracking

```cpp
struct FragmentTracker {
    uint16_t message_id;
    uint8_t  total_fragments;
    uint8_t  received_count;
    uint8_t  received_mask[32];  // Bitmask: up to 256 fragments
    double   first_recv_time;
    double   last_recv_time;
    int      buffer_offset;     // Offset into reassembly buffer pool
    int      buffer_capacity;   // Total reassembly buffer size

    bool is_complete() const {
        return received_count == total_fragments;
    }

    int next_missing() const {
        // SIMD: _tzcnt on inverted mask words
        return g_simd->fragment_next_missing(received_mask, total_fragments);
    }
};
```

### REQ-08.5: Fragment-Level Retransmission

When a reliable fragmented message has missing fragments:
1. Sender tracks which fragments were acked (via packet-level ack)
2. Only **missing** fragments are retransmitted (not the whole message)
3. Each fragment retransmit follows the same RTT-adaptive backoff as regular messages

This is an improvement over UE5 (which retransmits all fragments on any loss).

### REQ-08.6: Reassembly Buffer

Pre-allocated per connection:
```cpp
// Per connection: max_concurrent_fragments reassembly slots
// Each slot: max_message_size bytes
FixedRingBuffer<FragmentTracker, 16> active_reassemblies;
Pool<uint8_t[MAX_MESSAGE_SIZE]> reassembly_buffers;
```

### REQ-08.7: Fragment Timeout

If a fragmented message is not fully reassembled within `fragment_timeout` (default 5000ms):
1. Release the reassembly buffer back to pool
2. Discard all received fragments for that message_id
3. Increment `stats.fragments_timed_out`

### REQ-08.8: Fragment Cleanup

On every `update()` call, scan active reassemblies and clean up timed-out entries.
Use `g_simd->fragment_bitmask_complete()` for SIMD-accelerated completion check.

## Scenarios

#### Scenario: 64KB message fragmentation
Given MTU=1200 and max_fragment_payload=1150 bytes
When app sends a 65536-byte reliable message
Then it is split into ceil(65536/1150) = 57 fragments
Each fragment has FragmentHeader with message_id=N, fragment_count=57

#### Scenario: All fragments arrive in order
Given 57 fragments sent for message_id=42
When all 57 arrive in order
Then reassembly completes and full 65536-byte message is delivered to app

#### Scenario: Fragment loss + retransmit
Given 57 fragments sent, fragments 10 and 30 are lost
When sender detects loss via ack_bits
Then only fragments 10 and 30 are retransmitted (not all 57)
When retransmitted fragments arrive
Then reassembly completes

#### Scenario: Fragment timeout
Given 57 fragments sent, fragment 50 never arrives (reliable exhausted)
When 5000ms passes since first fragment received
Then reassembly buffer is released
And stats.fragments_timed_out incremented
