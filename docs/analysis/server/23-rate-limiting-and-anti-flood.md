# Rate Limiting and Anti-Flood

**File:** `NetManager.cs` (lines 393-428)

### 23.1 Per-Connection Limit

```csharp
public static int MaxPacketsPerSecond = 1000;

// In the receive thread, for each data packet:
conn.PacketsPerSecond++;

if (conn.PacketsPerSecond > MaxPacketsPerSecond) {
    if (packetPerSecondElapsed >= conn.PacketsPerSecondTimeout) {
        // Reset — 1-second window expired
        conn.PacketsPerSecond = 0;
        conn.PacketsPerSecondTimeout = elapsed + TimeSpan.FromSeconds(1);
        // Processes normally
    } else {
        // EXCEEDED → DISCONNECTS IMMEDIATELY
        Connections.Remove(conn.RemoteEndPoint);
        // Sends disconnect packet
        buffer.Put((byte)PacketType.Disconnected);
        conn.Manager.EnqueueEvent(disconnectPacket);
    }
}
```

**Policy:** More than 1000 packets/second = disconnect. No warning, no soft throttle.

### 23.2 PacketThrottler (COMMENTED OUT)

```csharp
// Was commented out in the code, but the idea was:
public bool CanProcess(ClientPacketType type, int maxFrequency) {
    ref PacketTimeoutData data = ref PacketFrequency[(uint)type];
    ++data.Frequency;
    if (data.Frequency > maxFrequency) {
        if (TickNumber >= data.TimeoutTick) {
            data.Frequency = 0;
            data.TimeoutTick = TickNumber + 32;  // 1-second cooldown
            return true;
        }
        return false;  // Throttled
    }
    return true;
}
```

Would allow limiting frequency **per packet type** (ex: max 10 MoveTo per second).

