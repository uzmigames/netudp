# Reliable Ordered Delivery

**File:** `Connection.cs` (lines 288-360)

```csharp
if (buffer.Sequence == NextRemoteSequence) {
    // In order — delivers immediately
    PacketReceived?.Invoke(buffer);
    NextRemoteSequence = (short)((NextRemoteSequence + 1) % WindowSize);

    // Drains consecutive buffered packets
    while (RemoteReliableOrderBuffer.Remove(NextRemoteSequence, out ByteBuffer next)) {
        PacketReceived?.Invoke(next);
        NextRemoteSequence = (short)((NextRemoteSequence + 1) % WindowSize);
        ConcurrentByteBufferPool.Release(next);
    }
} else {
    // Out of order — buffers it
    RemoteReliableOrderBuffer.Add(sequence, buffer);

    // Overflow protection
    if (RemoteReliableOrderBuffer.Count > 200) {
        Disconnect(DisconnectReason.RemoteBufferTooBig);
    }
}
```

**Reorder limit:** 200 out-of-order packets → forced disconnect.

