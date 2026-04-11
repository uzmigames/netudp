# VarInt and ZigZag Encoding

**File:** `ByteBuffer.cs` (lines 595-655)

### 19.1 ZigZag (int → uint)

Maps signed integers to unsigned integers, preserving small values:
```
 0 →  0
-1 →  1
 1 →  2
-2 →  3
 2 →  4
...
```

```csharp
// Encode: int → uint
uint zigzag = (uint)((value << 1) ^ (value >> 31));

// Decode: uint → int
int zagzig = (int)((value >> 1) ^ (-(int)(value & 1)));
```

### 19.2 LEB128 (Variable-Length Encoding)

```csharp
public void PutVar(uint value) {
    do {
        uint buffer = value & 0x7Fu;  // 7 data bits
        value >>= 7;
        if (value > 0) buffer |= 0x80u;  // Continuation bit
        Put((byte)buffer);
    } while (value > 0);
}
// 0-127:     1 byte
// 128-16383: 2 bytes
// etc.

public uint GetVarUInt() {
    uint value = 0;
    int shift = 0;
    do {
        uint buffer = GetByte();
        value |= (buffer & 0x7Fu) << shift;
        shift += 7;
    } while ((buffer & 0x80u) > 0);
    return value;
}
```

### 19.3 Combined Usage

```csharp
// PutVar(int) = ZigZag + LEB128
public void PutVar(int value) {
    uint zigzag = (uint)((value << 1) ^ (value >> 31));
    PutVar(zigzag);
}

// Floats quantized as VarInt:
public void Put(float value) {
    PutVar((int)(value * 20.0f));  // 0.05 unit precision
}
```

