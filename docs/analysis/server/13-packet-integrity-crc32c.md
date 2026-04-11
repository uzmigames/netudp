# Packet Integrity (CRC32C)

**File:** `GameServer/Shared/Networking/CRC32C.cs` (153 lines)

### 13.1 Three Implementations

**Hardware SSE4.2 (x86/x64):**
```csharp
if (Sse42.X64.IsSupported) {
    // Processes 8 bytes per iteration
    ulong crclong = crcLocal;
    for (int i = 0; i < ulongs.Length; i++)
        crclong = Sse42.X64.Crc32(crclong, ulongs[i]);
    crcLocal = (uint)crclong;
}
```

**Hardware ARM CRC32 (ARM64):**
```csharp
if (Crc32.Arm64.IsSupported) {
    for (int i = 0; i < ulongs.Length; i++)
        crcLocal = Crc32.Arm64.ComputeCrc32C(crcLocal, ulongs[i]);
}
```

**Software (slicing-by-16):**
```csharp
// Table of 16 × 256 pre-computed entries
while (length >= 16) {
    // 4 parallel table lookups
    var a = Table[(3*256)+input[offset+12]] ^ ... ;
    var b = Table[(7*256)+input[offset+8]]  ^ ... ;
    var c = Table[(11*256)+input[offset+4]] ^ ... ;
    var d = Table[(15*256)+(crcLocal^input[offset])] ^ ... ;
    crcLocal = d ^ c ^ b ^ a;
    offset += 16; length -= 16;
}
```

### 13.2 Usage

```csharp
// Envio (send thread):
uint crc32c = CRC32C.Compute(buffer.Data, buffer.Position);
buffer.Put(crc32c);  // Append 4 bytes

// Recepcao (receive thread):
var crc32c = CRC32C.Compute(buffer.Data, buffer.Size - 4);
buffer.Size -= 4;
if (*((uint*)(buffer.Data + buffer.Size)) == crc32c) {
    // Valido → processar
} else {
    // Invalido → descartar silenciosamente
}
```

