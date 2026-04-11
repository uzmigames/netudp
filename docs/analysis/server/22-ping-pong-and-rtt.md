# Ping/Pong and RTT System

**File:** `NetManager.cs` (lines 740-804, 288-318)

### 22.1 Ping Sending (Receive Thread)

```csharp
// Every 5 seconds:
if (PingTimer.Elapsed >= TimeSpan.FromSeconds(5)) {
    foreach (var kv in Connections) {
        kv.Value.PingSentAt = DateTime.UtcNow;

        ByteBuffer pingBuffer = ConcurrentByteBufferPool.Acquire();
        pingBuffer.Connection = kv.Value;
        pingBuffer.Reliable = false;
        pingBuffer.Put((byte)PacketType.Ping);

        Send(pingBuffer);
    }
    PingTimer.Restart();
}
```

### 22.2 Pong Reception

```csharp
case PacketType.Pong:
    conn.Ping = (int)(DateTime.UtcNow - conn.PingSentAt).TotalMilliseconds;
    break;
```

### 22.3 Limitations

- **No smoothing** (SRTT, RTTVAR) — raw value replaced on each measurement
- **Does not influence retransmission** — timeout is fixed at 150ms
- **Does not influence anything** — `conn.Ping` is informational only
- Coarse interval of **5 segundos**
- Default value: **50ms** (hardcoded)

