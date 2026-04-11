# 8. LZ4 Compression

**File:** `Core/Utils/LZ4.cs` (229 lines)

## Overview

Custom pure-C# LZ4 implementation (not the standard library). Applied **after encryption** for packets > 512 bytes. Server1 had no compression at all.

## Algorithm

Standard LZ4 block format:
- Hash table with 65536 entries (`1 << 16`)
- 4-byte minimum match length
- Token byte: high nibble = literal length, low nibble = match length
- Offset stored as 2-byte little-endian (max 65535 bytes back-reference)

## Key Implementation Details

```csharp
private const int MinMatch = 4;
private const int HashLog = 16;
private const int HashSize = 1 << HashLog;  // 65536
private const uint HashMultiplier = 2654435761u;  // Knuth's multiplicative hash

// Hash table allocated on stack (256KB):
uint* hashTable = stackalloc uint[HashSize];

// Hash function:
private static int Hash(uint value) {
    return (int)((value * HashMultiplier) >> ((MinMatch * 8) - HashLog));
}
```

## Integration with Encryption

```
Plaintext → ChaCha20-Poly1305 → Ciphertext+Tag
                                      │
                                      ▼ (if > 512 bytes)
                                    LZ4.Compress()
                                      │
                                      ▼ (if smaller)
                                  Use compressed
                                      │
                                      ▼ (if not smaller)
                                  Use uncompressed
```

The `Compressed` flag in `PacketHeaderFlags` tells the receiver whether to decompress before decrypting.

## Compression of Encrypted Data

Normally, encrypted data is incompressible (looks random). However, game packets contain **structured data with patterns** that survive encryption because:
- LZ4 looks for **repeated 4-byte sequences** at matching hash positions
- ChaCha20 is a stream cipher — identical plaintext offsets with different nonces don't produce identical ciphertext
- But **the encrypted overhead (nonce/tag)** and **packet structure** can still have compressible patterns when multiple sub-messages are batched

The 512-byte threshold avoids the overhead for small packets where compression gain is unlikely.

## Performance

- `stackalloc` for hash table — zero heap allocation
- Unsafe pointer arithmetic throughout
- `[MethodImpl(MethodImplOptions.AggressiveInlining)]` on hot functions
- Maps perfectly to C implementation for netudp
