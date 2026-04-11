# Spec 15 — Network Simulator

## Requirements

### REQ-15.1: Configuration

```cpp
struct netudp_simulator_config_t {
    float latency_ms;           // Added one-way latency (constant)
    float jitter_ms;            // Random ± jitter on top of latency
    float packet_loss_pct;      // 0..100 chance to drop outgoing
    float duplicate_pct;        // 0..100 chance to duplicate outgoing
    float out_of_order_pct;     // 0..100 chance to swap with adjacent
    float incoming_lag_min_ms;  // Min additional receive delay
    float incoming_lag_max_ms;  // Max additional receive delay
};
```

### REQ-15.2: Integration

Enabled per-instance via config or runtime:
```cpp
void netudp_server_set_simulator(netudp_server_t* server, const netudp_simulator_config_t* cfg);
void netudp_client_set_simulator(netudp_client_t* client, const netudp_simulator_config_t* cfg);
```

Pass `NULL` to disable simulator. Simulator has ZERO overhead when disabled.

### REQ-15.3: Implementation

The simulator intercepts packets between the send/recv pipeline and the actual socket:
- Outgoing: after encrypt, before socket send
- Incoming: after socket recv, before decrypt

Delayed packets stored in a pre-allocated ring buffer sorted by delivery time.

## Scenarios

#### Scenario: Simulate 50ms RTT with 5% loss
Given simulator config: latency=25ms, jitter=5ms, loss=5%
When 1000 packets are sent
Then ~950 arrive, with latency between 20-30ms each way
And RTT measured by the library ≈ 40-60ms
