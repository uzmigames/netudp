# 8. Connection Statistics

## Real-Time Status

```cpp
struct SteamNetConnectionRealTimeStatus_t {
    ESteamNetworkingConnectionState m_eState;   // Connection state
    int m_nPing;                                // Current ping (ms)
    float m_flConnectionQualityLocal;           // 0..1 (packet delivery %)
    float m_flConnectionQualityRemote;          // 0..1 (as observed by remote)
    float m_flOutPacketsPerSec;                 // Current send rate
    float m_flOutBytesPerSec;                   // Current send throughput
    float m_flInPacketsPerSec;                  // Current recv rate
    float m_flInBytesPerSec;                    // Current recv throughput
    int m_nSendRateBytesPerSecond;              // Estimated channel capacity
    int m_cbPendingUnreliable;                  // Bytes queued (unreliable)
    int m_cbPendingReliable;                    // Bytes queued (reliable)
    int m_cbSentUnackedReliable;                // Bytes on wire, unacked
    SteamNetworkingMicroseconds m_usecQueueTime; // Estimated queue wait
};
```

## What This Enables

- **Adaptive send rate** — application can check `m_nSendRateBytesPerSecond` and throttle
- **Quality monitoring** — `m_flConnectionQualityLocal` detects degrading connections
- **Queue management** — `m_cbPendingReliable` prevents unbounded queue growth
- **Latency awareness** — `m_usecQueueTime` tells when data will actually be sent
- **Detailed debugging** — `GetDetailedConnectionStatus()` returns human-readable text

## Relevance to netudp

netudp MUST provide equivalent statistics. Server1 had `conn.Ping` only. netcode.io has none. GNS shows the gold standard for connection quality metrics.
