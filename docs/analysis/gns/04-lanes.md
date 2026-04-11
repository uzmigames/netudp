# 4. Lanes — Multi-Stream Prioritization

## Overview

GNS calls them **lanes** (not channels). Lanes are multiple logical message streams on the same connection, each with independent:
- Priority (strict ordering between lanes)
- Weight (weighted fair queueing within same priority)
- Queue statistics
- Head-of-line blocking isolation

## Configuration

```cpp
EResult ConfigureConnectionLanes(
    HSteamNetConnection hConn,
    int nNumLanes,
    const int *pLanePriorities,      // Strict priority (lower = higher priority)
    const uint16 *pLaneWeights       // Weight within same priority level
);
```

## Priority + Weight Scheduling

```
Lane 0: Priority 0, Weight 1  ← Highest priority, gets all bandwidth first
Lane 1: Priority 1, Weight 3  ← Second priority, 3/4 of remaining bandwidth
Lane 2: Priority 1, Weight 1  ← Second priority, 1/4 of remaining bandwidth
Lane 3: Priority 2, Weight 1  ← Lowest priority, only when lanes 0-2 are empty
```

This is a combination of strict priority scheduling and weighted fair queueing — extremely flexible.

## Per-Lane Statistics

```cpp
struct SteamNetConnectionRealTimeLaneStatus_t {
    int m_cbPendingUnreliable;      // Bytes queued unreliable
    int m_cbPendingReliable;        // Bytes queued reliable
    int m_cbSentUnackedReliable;    // Bytes on wire, unacked
    SteamNetworkingMicroseconds m_usecQueueTime;  // Estimated wait time
};
```

## Use Cases

- **Lane 0 (high priority):** Game inputs, RPCs — small, latency-critical
- **Lane 1 (medium):** Game state updates — regular throughput
- **Lane 2 (low):** Asset streaming, voice chat — bulk, latency-tolerant

## Comparison with Channel Systems

| Aspect | Typical channels (ENet) | GNS Lanes |
|---|---|---|
| Scheduling | Round-robin or FIFO | Priority + weighted fair queue |
| Stats | None | Per-lane queue depth, wait time |
| Reliability | Per-channel (all reliable or all unreliable) | **Per-message** (any mix per lane) |
| Head-of-line blocking | Per-channel | Per-lane |
| Config | Fixed at creation | **Reconfigurable at runtime** |

## Relevance to netudp

netudp should support configurable channels (our term, not "lanes") with:
- Per-channel reliability mode (netcode.io style — simpler than per-message)
- Priority ordering between channels
- Optional weight-based scheduling for same-priority channels
- Per-channel statistics
