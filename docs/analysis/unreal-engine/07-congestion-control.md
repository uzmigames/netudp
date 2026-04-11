# 7. Congestion & Bandwidth Control

## Token Bucket (QueuedBits)
- Per-connection `QueuedBits` counter
- Each tick: increment by `(CurrentNetSpeed / 8) * DeltaTime`
- When `QueuedBits > 0`, connection can't send until next tick
- Send rate is per-connection, not global

## What's Missing (vs modern protocols)
- No explicit CWND estimation (QUIC-style)
- No RTT-based adaptive retransmit (static or simple)
- No packet pacing (entire buffer flushed in one call)
- No loss-triggered congestion window adjustment

## Packet Simulation (Debug)
```cpp
FPacketSimulationSettings:
    PktLoss          // % to drop
    PktLag           // Constant or variable delay
    PktDup           // % to duplicate
    PktOrder         // Randomize order
    PktIncomingLagMin/Max  // Receive delay range
```

## netudp improvement
netudp adds loss-based AIMD congestion avoidance on top of token bucket, plus continuous RTT measurement from ack delay field.
