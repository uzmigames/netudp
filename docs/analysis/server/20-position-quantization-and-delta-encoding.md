# Position Quantization and Delta Encoding

**File:** `ByteBuffer.cs` (lines 332-397)

### 20.1 Constantes

```csharp
const float FloatQuantizeFactor    = 1.0f / 0.05f;  // = 20 (precision 0.05)
const float FloatDequantizeFactor  = 0.05f;

const float PositionQuantizeFactor   = 1.0f / 0.1f;  // = 10 (precision 0.1)
const float PositionDequantizeFactor = 0.1f;

const float QuantizeFactor   = short.MaxValue / (ChunkSize * 2);  // 32767/64 ≈ 512
const float DequantizeFactor = (ChunkSize * 2) / short.MaxValue;  // 64/32767 ≈ 0.00195
```

### 20.2 Position with Delta Encoding

```csharp
public void PutPosition(Vector2 value) {
    int x = (int)(value.X * 10.0f) - QuantizeOffsetX;  // Delta from offset
    int y = (int)(value.Y * 10.0f) - QuantizeOffsetY;

    PutVar(x);  // VarInt — few bytes if delta is small
    PutVar(y);

    QuantizeOffsetX = x;  // Updates offset for next delta
    QuantizeOffsetY = y;
}

public Vector2 GetPosition() {
    int x = GetVarInt() + QuantizeOffsetX;
    int y = GetVarInt() + QuantizeOffsetY;
    QuantizeOffsetX = x;
    QuantizeOffsetY = y;
    return new Vector2(x * 0.1f, y * 0.1f);
}
```

**Result:** Nearby positions (within 1-2 units of last value) encoded in **2-4 bytes** instead of 8 bytes (two floats).

### 20.3 Position Quantized to Short (Chunk-Relative)

```csharp
public void PutQuantized(Vector2 value) {
    Put((short)((value.X - QuantizeOffsetX) * 512.0f));  // Relative to chunk
    Put((short)((value.Y - QuantizeOffsetY) * 512.0f));
}
// Always 4 bytes, but with larger range within the chunk (64 units)
```

### 20.4 Rotation

```csharp
// Rotation compressed to 1 byte (256 values for 2π)
public float GetRotation() {
    return (GetByte() / 40.584f) - MathF.PI;
    // 0 → -π, 128 → 0, 255 → +π
}
```

