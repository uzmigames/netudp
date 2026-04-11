# 3. Packet Header & Wire Format

**File:** `Core/Network/PacketHeader.cs` (70 lines)

## Structured Packet Header (14 bytes)

Major upgrade from Server1's 1-byte type-only header.

```csharp
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct PacketHeader {
    public uint ConnectionId;      // 4 bytes LE — identifies connection
    public PacketChannel Channel;  // 1 byte — delivery semantics
    public PacketHeaderFlags Flags;// 1 byte — bitfield
    public ulong Sequence;         // 8 bytes LE — crypto/reliability sequence

    public const int Size = 14;
}
```

## Channel Types (NEW — Server1 only had 2)

```csharp
public enum PacketChannel : byte {
    Unreliable = 0,         // Fire-and-forget
    ReliableOrdered = 1,    // Guaranteed, in-order
    ReliableUnordered = 2   // Guaranteed, any order (NEW)
}
```

## Header Flags (Bitfield)

```csharp
[Flags]
public enum PacketHeaderFlags : byte {
    None                    = 0,
    Encrypted               = 1 << 0,  // Payload is encrypted
    AEAD_ChaCha20Poly1305   = 1 << 1,  // Specific cipher used
    Rekey                   = 1 << 2,  // Key rotation in progress
    Fragment                = 1 << 3,  // This is a fragment
    Compressed              = 1 << 4,  // LZ4 compressed after encryption
    Acknowledgment          = 1 << 5,  // This is an ACK
    ReliableHandshake       = 1 << 6   // Crypto handshake packet
}
```

## Wire Format — Encrypted Packet

```
Offset  Size    Field
0       14      PacketHeader (ConnectionId + Channel + Flags + Sequence)
                  ← This is the AAD (Authenticated Associated Data)
14      N       Encrypted payload (ChaCha20-Poly1305 ciphertext)
14+N    16      Poly1305 authentication tag
────────────────
Total: 30 + N bytes

If Compressed flag set:
14      M       LZ4(encrypted payload + tag)
────────────────
Total: 14 + M bytes (M < N + 16)
```

## Wire Format — Legacy Packet (unencrypted)

```
Offset  Size    Field
0       1       PacketType byte
1       N       Payload
1+N     4       CRC32C
────────────────
Total: 5 + N bytes
```

## AAD (Authenticated Associated Data)

The entire 14-byte header is used as AAD for AEAD encryption:

```csharp
public ReadOnlySpan<byte> GetAAD() {
    unsafe {
        fixed (PacketHeader* ptr = &this)
            return new ReadOnlySpan<byte>(ptr, Size);  // All 14 bytes
    }
}
```

This means the header is **authenticated but not encrypted** — a receiver can read ConnectionId, Channel, Flags, and Sequence without decrypting, but any tampering is detected.

## Transport Packet Types (17 total)

```csharp
public enum PacketType : byte {
    Connect             = 0,
    Ping                = 1,
    Pong                = 2,
    Reliable            = 3,
    Unreliable          = 4,
    Ack                 = 5,
    Disconnect          = 6,
    Error               = 7,
    ConnectionDenied    = 8,
    ConnectionAccepted  = 9,
    CheckIntegrity      = 10,
    BenckmarkTest       = 11,
    Fragment            = 12,   // NEW
    Cookie              = 13,   // NEW
    CryptoTest          = 14,   // NEW
    CryptoTestAck       = 15,   // NEW
    ReliableHandshake   = 16,   // NEW
    None                = 255
}
```
