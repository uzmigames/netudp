# Unreliable Delivery

**File:** `Connection.cs` (lines 201-247, 366-380)

```csharp
// Envio:
ByteBuffer BeginUnreliable() {
    UnreliableBuffer = ConcurrentByteBufferPool.Acquire();
    UnreliableBuffer.Put((byte)PacketType.Unreliable);
    HeaderWriter.PutUnreliableHeader(UnreliableBuffer);  // Custom header
    UnreliableBuffer.Put(Manager.TickNumber);             // Tick for temporal ordering
    UnreliableBuffer.Reliable = false;
    return UnreliableBuffer;
}

// Recepcao:
case PacketType.Unreliable:
    HeaderWriter?.ReadUnreliableHeader(buffer);
    PacketReceived?.Invoke(buffer);  // Direct delivery, no reordering
    break;
```

**No sequence number** in unreliable — uses only `TickNumber` for temporal reference. The client decides what to do with "stale" packets.

