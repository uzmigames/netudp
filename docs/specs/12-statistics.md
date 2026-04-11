# Spec 12 — Connection Statistics & Diagnostics

## Requirements

### REQ-12.1: Per-Connection Stats

```cpp
struct netudp_connection_stats_t {
    // Timing
    uint32_t ping_ms;                     // Current RTT (smoothed)
    float    connection_quality_local;     // 0..1 (packet delivery rate, local observation)
    float    connection_quality_remote;    // 0..1 (as reported by remote via ack)

    // Throughput
    float    out_packets_per_sec;
    float    out_bytes_per_sec;
    float    in_packets_per_sec;
    float    in_bytes_per_sec;

    // Capacity
    uint32_t send_rate_bytes_per_sec;     // Current allowed send rate (after congestion)
    uint32_t max_send_rate_bytes_per_sec; // Configured maximum

    // Queue depth
    uint32_t pending_unreliable_bytes;    // Queued unreliable (will be sent or dropped)
    uint32_t pending_reliable_bytes;      // Queued reliable (will be retransmitted until acked)
    uint32_t sent_unacked_reliable_bytes; // On wire but not yet acked
    uint64_t queue_time_us;              // Estimated wait before next send

    // Reliability
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t packets_lost;                // Detected via ack gaps
    uint32_t packets_out_of_order;
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t messages_dropped;            // Reliable retransmit exhausted
    uint32_t window_stalls;               // Times send blocked by full ack window

    // Fragments
    uint32_t fragments_sent;
    uint32_t fragments_received;
    uint32_t fragments_retransmitted;
    uint32_t fragments_timed_out;

    // Compression (if enabled)
    float    compression_ratio;           // bytes_out / bytes_in (0 if disabled)
    uint64_t compression_bytes_saved;

    // Security
    uint32_t replay_attacks_blocked;
    uint32_t decrypt_failures;
};
```

### REQ-12.2: Per-Channel Stats

```cpp
struct netudp_channel_stats_t {
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t messages_dropped;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t pending_bytes;
    uint32_t unacked_reliable_bytes;
    uint64_t queue_time_us;
    float    compression_ratio;
};
```

### REQ-12.3: Global Server Stats

```cpp
struct netudp_server_stats_t {
    uint32_t connected_clients;
    uint32_t max_clients;
    uint64_t total_packets_sent;
    uint64_t total_packets_received;
    uint64_t total_bytes_sent;
    uint64_t total_bytes_received;
    float    packets_per_sec_in;
    float    packets_per_sec_out;
    // DDoS
    uint8_t  ddos_severity;
    uint32_t ddos_bad_packets_per_sec;
};
```

### REQ-12.4: API

```cpp
int netudp_server_connection_status(netudp_server_t* server, int client_index,
                                     netudp_connection_stats_t* out);

int netudp_server_channel_status(netudp_server_t* server, int client_index,
                                  int channel, netudp_channel_stats_t* out);

int netudp_server_stats(netudp_server_t* server, netudp_server_stats_t* out);
```

All stat queries SHALL be O(1) — stats are accumulated during `update()`, queries just copy the struct.

### REQ-12.5: Throughput Calculation

Throughput counters use exponential moving average over 1-second windows:

```cpp
// Every second:
out_bytes_per_sec = 0.8f * out_bytes_per_sec + 0.2f * bytes_sent_this_second;
```

## Scenarios

#### Scenario: Query connection stats
Given client connected with 50ms RTT and 2% packet loss
When `netudp_server_connection_status(server, 0, &stats)`
Then `stats.ping_ms ≈ 50`
And `stats.connection_quality_local ≈ 0.98`

#### Scenario: Compression ratio tracking
Given netc enabled with trained dictionary
When 1000 packets compressed with average ratio 0.57
Then `stats.compression_ratio ≈ 0.57`
And `stats.compression_bytes_saved > 0`
