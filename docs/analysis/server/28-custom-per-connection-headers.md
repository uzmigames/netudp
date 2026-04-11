# IHeaderWriter — Custom Per-Connection Headers

**File:** `Connection.cs` (lines 30-34), `PlayerController.cs` (lines 43-55)

### 28.1 Interface

```csharp
public interface IHeaderWriter {
    void PutUnreliableHeader(ByteBuffer buffer);
    void ReadUnreliableHeader(ByteBuffer buffer);
}
```

### 28.2 PlayerController Implementation

```csharp
void IHeaderWriter.PutUnreliableHeader(ByteBuffer buffer) {
    // Sets quantization offsets based on the player's chunk
    buffer.QuantizeOffsetX = QuantizeOffset.X;
    buffer.QuantizeOffsetY = QuantizeOffset.Y;

    // Writes the chunk offset as 2 bytes (sbyte each)
    buffer.Put((sbyte)(QuantizeOffset.X >> 5));  // ChunkSizeLog = 5
    buffer.Put((sbyte)(QuantizeOffset.Y >> 5));
}

void IHeaderWriter.ReadUnreliableHeader(ByteBuffer buffer) {
    // Client doesn't need to read (server → client)
}
```

**Purpose:** Allows positions in unreliable packets to be relative to the player's chunk, reducing the required range and improving compression.

