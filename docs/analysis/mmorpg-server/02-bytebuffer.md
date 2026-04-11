# 2. ByteBuffer — TypeScript Implementation

**File:** `src/engine/core/bytebuffer.ts`

## Overview

Pure TypeScript ByteBuffer using `Uint8Array`. Dynamic resizing (unlike the C# versions which use fixed-capacity native memory).

## Key Differences from C# Versions

| Aspect | C# (Server1/5) | TypeScript (this) |
|---|---|---|
| Memory | NativeMemory / Marshal.AllocHGlobal | Uint8Array (JS GC managed) |
| Capacity | Fixed at creation | Dynamic (`ensureCapacity`) |
| Pointer access | `byte*` unsafe | DataView for multi-byte reads |
| Endianness | Implicit (x86 = LE) | Explicit (`setInt32(0, value, true)`) |
| VarInt | Yes (ZigZag + LEB128) | No (uses fixed-size types) |
| Quantization | Position/rotation quantized | Vectors as 3x float32 (no quantization) |
| Strings | UTF-8 with ushort length | UTF-8 with int32 length |

## Serialization Methods

```typescript
putByte(value: number)          // 1 byte
putBool(value: boolean)         // 1 byte (0/1)
putInt32(value: number)         // 4 bytes, little-endian
putUInt32(value: number)        // 4 bytes, little-endian
putFloat(value: number)         // 4 bytes, float32, little-endian
putString(value: string)        // int32 length + UTF-8 bytes
putVector({x, y, z})            // 3x float32 = 12 bytes
putRotator({pitch, yaw, roll})  // 3x float32 = 12 bytes
putId(id: string)               // GUID string → int32 via lookup
```

## Dynamic Capacity

```typescript
private ensureCapacity(requiredBytes: number) {
    const requiredCapacity = this.position + requiredBytes;
    if (requiredCapacity > this.buffer.length) {
        const newBuffer = new Uint8Array(requiredCapacity);
        newBuffer.set(this.buffer, 0);
        this.buffer = newBuffer;
    }
}
```

Grows exactly to needed size. No power-of-two rounding. Simple but causes frequent re-allocations.

## Packet Splitting (for batched packets)

```typescript
static splitPackets(combinedBuffer: ByteBuffer): ByteBuffer[] {
    // Split on 0xFE 0xFE 0xFE 0xFE delimiter
    const packets: ByteBuffer[] = [];
    // Scan for 4-byte sentinel...
    return packets;
}
```

Uses a 4-byte magic sentinel `0xFEFEFEFE` to delimit sub-packets within a batched packet. This is a simpler approach than Server1's MTU-based batching.

## Data-Driven Serialization

```typescript
public writeDataToBuffer(dataSequence: Map<string, string>, values: Map<string, any>) {
    dataSequence.forEach((type, key) => {
        const value = values.get(key);
        switch (type) {
            case 'id': this.putId(value); break;
            case 'int32': this.putInt32(value); break;
            case 'float': this.putFloat(value); break;
            case 'string': this.putString(value); break;
            case 'byte': this.putByte(value); break;
            case 'bool': this.putBool(value); break;
            case 'vector': this.putVector(value); break;
            case 'rotator': this.putRotator(value); break;
        }
    });
}
```

Schema-driven serialization using a type map — a simpler version of Server5's Contract system.

## No Quantization

Vectors sent as 3x float32 (12 bytes each). No short quantization like Server1 (6 bytes) or Server5 (6 bytes). This is acceptable over WebSocket/TCP since bandwidth is less constrained than UDP (no MTU limit, TCP handles fragmentation).
