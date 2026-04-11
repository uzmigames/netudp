# Spec 10 — Bandwidth Control & Congestion

## Requirements

### REQ-10.1: Per-Connection Token Bucket

```cpp
struct TokenBucket {
    uint32_t rate_bytes_per_sec;   // Refill rate
    uint32_t burst_bytes;          // Max burst
    double   tokens;               // Current available
    int64_t  last_refill_us;

    bool try_consume(int bytes);
    void refill(int64_t now_us);
};
```

Default: 256 KB/s rate, 32 KB burst. Configurable per connection.

### REQ-10.2: QueuedBits Per-Connection Budget

```cpp
int32_t queued_bits;  // Tracks budget per tick

// Each tick:
queued_bits -= (send_rate_bps * delta_time);
queued_bits = max(queued_bits, -burst_bits);  // Allow negative = available budget

// Each send:
queued_bits += packet_size_bits;

// If queued_bits > 0: defer remaining sends to next tick
```

### REQ-10.3: AIMD Congestion Control

Track loss rate from ack_bits over sliding window (last 64 packets):

```cpp
float loss_rate = (float)lost_packets / total_packets;

if (loss_rate > 0.05f) {
    // Multiplicative Decrease: reduce by 25%
    send_rate_bytes_per_sec = (uint32_t)(send_rate_bytes_per_sec * 0.75f);
    send_rate_bytes_per_sec = max(send_rate_bytes_per_sec, MIN_SEND_RATE);
}
else if (loss_rate < 0.01f && rtt_samples >= 10) {
    // Additive Increase: add 10% (capped at configured max)
    send_rate_bytes_per_sec = min(
        (uint32_t)(send_rate_bytes_per_sec * 1.10f),
        max_send_rate_bytes_per_sec
    );
}
```

Constants:
```cpp
static constexpr uint32_t MIN_SEND_RATE   = 32 * 1024;    // 32 KB/s
static constexpr uint32_t DEFAULT_MAX_RATE = 256 * 1024;   // 256 KB/s
static constexpr float    DECREASE_FACTOR  = 0.75f;
static constexpr float    INCREASE_FACTOR  = 1.10f;
static constexpr float    LOSS_THRESHOLD_HIGH = 0.05f;
static constexpr float    LOSS_THRESHOLD_LOW  = 0.01f;
```

### REQ-10.4: Per-IP Pre-Connection Rate Limit

Applied BEFORE connect token processing (DDoS layer 1):

```cpp
static constexpr int PRE_CONN_RATE  = 60;   // packets/sec per IP
static constexpr int PRE_CONN_BURST = 10;   // burst allowance
```

Uses `FixedHashMap<AddressHash, TokenBucket, 4096>`. Entries expire after 30 seconds of inactivity.

### REQ-10.5: DDoS Severity Escalation

```cpp
enum class DDoSSeverity : uint8_t { None=0, Low=1, Medium=2, High=3, Critical=4 };

struct DDoSMonitor {
    DDoSSeverity severity;
    int bad_packets_per_sec;
    double cooloff_timer;

    void on_bad_packet();
    void update(double dt);
    bool should_process_new_connection() const;
    bool should_process_packet(bool is_established) const;
};
```

| Severity | Trigger | Action | Cooloff |
|---|---|---|---|
| None | default | Normal operation | — |
| Low | >100 bad/sec | Log warning | 30s |
| Medium | >500 bad/sec | Reduce processing budget 50% | 30s |
| High | >2000 bad/sec | Drop all non-established packets | 30s |
| Critical | >10000 bad/sec | Reject ALL new connections | 60s |

## Scenarios

#### Scenario: Bandwidth limit enforcement
Given connection with send_rate = 256 KB/s
When app tries to send 512 KB in one tick
Then first 256 KB is sent, remaining 256 KB queued for next tick

#### Scenario: Congestion decrease on loss
Given send_rate = 200 KB/s and loss_rate = 8%
When congestion control evaluates
Then send_rate reduced to 150 KB/s (200 × 0.75)

#### Scenario: Congestion increase on good conditions
Given send_rate = 150 KB/s, loss_rate = 0.5%, 10+ RTT samples
When congestion control evaluates
Then send_rate increased to 165 KB/s (150 × 1.10)
