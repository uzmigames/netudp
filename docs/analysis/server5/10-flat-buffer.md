# 10. FlatBuffer — Binary Serialization

**File:** `Core/Network/FlatBuffer.cs` (532 lines)

Struct (not class) with NativeMemory via `Marshal.AllocHGlobal`. Generic `Write<T>/Read<T>` for any unmanaged type. VarInt (ZigZag + LEB128) for int32/int64. Bit-level I/O (WriteBit/ReadBit). 3D vector quantization: FVector(X,Y,Z) → 3 shorts at 0.1 precision. FRotator(Pitch,Yaw,Roll) → 3 shorts. ASCII and UTF-8 string support. Save/Restore position for seek.
