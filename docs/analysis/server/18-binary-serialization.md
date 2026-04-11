# Binary Serialization (ByteBuffer)

**File:** `GameServer/Shared/Networking/ByteBuffer.cs` (1282 lines)

### 18.1 Primitive Types — Direct Pointer Access

```csharp
// Write (zero-copy, no bounds check)
public void Put(int value)    { *(int*)(Data + Position) = value;    Position += 4; }
public void Put(uint value)   { *(uint*)(Data + Position) = value;   Position += 4; }
public void Put(short value)  { *(short*)(Data + Position) = value;  Position += 2; }
public void Put(ushort value) { *(ushort*)(Data + Position) = value; Position += 2; }
public void Put(long value)   { *(long*)(Data + Position) = value;   Position += 8; }
public void Put(ulong value)  { *(ulong*)(Data + Position) = value;  Position += 8; }
public void Put(byte value)   { Data[Position] = value; Position++; }
public void Put(bool value)   { Data[Position] = (byte)(value ? 1 : 0); Position++; }

// Read (same approach)
public int GetInt()     { int r = *(int*)(Data + Position);     Position += 4; return r; }
public short GetShort() { short r = *(short*)(Data + Position); Position += 2; return r; }
public byte GetByte()   { byte r = Data[Position]; Position += 1; return r; }
```

### 18.2 Strings (Length-Prefixed UTF-8)

```csharp
public void Put(string value, int maxLength) {
    int length = Math.Min(value.Length, maxLength);
    int byteCountPosition = Position;
    Position += 2;  // Reserve 2 bytes for the length

    int bytesCount = Encoding.UTF8.GetBytes(value.AsSpan(),
        new Span<byte>(Data + Position, maxLength * 2));

    *(ushort*)(Data + byteCountPosition) = (ushort)bytesCount;
    Position += bytesCount;
}

public string GetString(int maxLength) {
    int bytesCount = GetUShort();
    if (bytesCount <= 0 || bytesCount > maxLength * 2) return string.Empty;
    string result = Encoding.UTF8.GetString(Data + Position, bytesCount);
    Position += bytesCount;
    return result;
}
```

### 18.3 Byte Arrays

```csharp
public void Put(byte[] value, int maxLength = 800) {
    int length = Math.Min(maxLength, value.Length);
    PutVar((uint)length);  // VarInt length prefix
    fixed (byte* src = value) {
        CustomCopy(Data + Position, src, length);  // Optimized copy 8 bytes at a time
    }
    Position += length;
}
```

### 18.4 Optimized Copy

```csharp
static unsafe void CustomCopy(void* dest, void* src, int count) {
    int block = count >> 3;  // 8-byte blocks
    long* pDest = (long*)dest;
    long* pSrc = (long*)src;
    for (int i = 0; i < block; i++) { *pDest = *pSrc; pDest++; pSrc++; }

    // Remaining bytes
    count = count - (block << 3);
    byte* pd = (byte*)pDest, ps = (byte*)pSrc;
    for (int i = 0; i < count; i++) { *pd = *ps; pd++; ps++; }
}
```

### 18.5 INetSerializable

```csharp
public interface INetSerializable {
    void Write(ByteBuffer buffer);
    void Read(ByteBuffer buffer);
}

// Generic usage:
public T Get<T>() where T : INetSerializable, new() {
    T instance = new T();
    instance.Read(this);
    return instance;
}
```

