# Reliability Engine

**File:** `Connection.cs` (lines 113-198), `NetManager.cs` (lines 486-506, 866-1077)

### 7.1 Sending Reliable Packets

```csharp
// Connection.BeginReliable()
Sequence = (short)((Sequence + 1) % NetConstants.WindowSize);  // Increments sequence
buffer.Put((byte)PacketType.Reliable);
buffer.Put(Sequence);                    // 2 bytes
buffer.Put(Manager.TickNumber);          // 4 bytes
buffer.Sequence = Sequence;
buffer.Reliable = true;

// Connection.EndReliable() — when buffer atinge MTU ou no Update()
ReliablePackets[buffer.Sequence] = buffer;  // Stores for retransmission
Send(buffer);
```

### 7.2 Retransmission

```csharp
// NetManager.Send() — marks for retransmission
Interlocked.Exchange(ref buffer.Acked, ReliableResendThreshold);  // = 30

// Send thread — loops every ReliableTimeout (150ms in production):
if (Interlocked.Decrement(ref ReliableList.Acked) <= 0) {
    // Acked reached 0 → remove (gave up or was acked)
    ConcurrentByteBufferPool.Release(ReliableList);
} else {
    // Resends the packet
    UDP.Unsafe.Send(udpSocket, &addr, buffer.Data, buffer.Size);
}
```

**Calculation:**
- `ReliableResendThreshold = 30`
- `ReliableTimeout = 150ms` (configurado em `Program.cs` linha 217)
- Maximum retransmissions: **30 vezes**
- Total window: 30 × 150ms = **4.5 segundos**
- If not acked within 4.5s, the packet is silently discarded

### 7.3 ACK Reception

```csharp
// Connection.ProcessPacket() — tipo Ack
while (buffer.HasData) {
    short sequence = buffer.GetShort();
    if (ReliablePackets.Remove(sequence, out ByteBuffer temp)) {
        Interlocked.Exchange(ref temp.Acked, 0);  // Stops retransmission
    }
}
```

