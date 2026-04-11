# 4. PacketHandler Pipeline (Middleware Chain)

## Architecture

The `PacketHandler` is a **chain-of-responsibility** pattern — each component can transform packets in sequence:

```
Outgoing:
  Game Data → FBitWriter
           ↓
  [PacketHandler Stack]
    • StatelessConnectHandlerComponent (handshake)
    • EncryptionComponent (AES-256-GCM)
    • OodleNetworkHandlerComponent (compression)
    • ReliabilityHandlerComponent (deprecated in 5.3+)
    • Custom HandlerComponents (user-defined)
           ↓
  Socket.SendTo()

Incoming:
  Socket.RecvFrom()
           ↓
  [PacketHandler Stack — reverse order]
           ↓
  NetConnection::ReceivedRawPacket()
```

## Handler States

```
Uninitialized → InitializingComponents → Initialized
                       ↓
             Components may be in InitializeOnRemote state
             (awaiting remote handshake completion)
```

## Key Pattern: Composable Middleware

Each `HandlerComponent` can:
- Read/write bits at packet level
- Buffer packets during initialization
- Inject handshake packets
- Track per-packet traits (encryption state, compression ratio)

```cpp
struct ProcessedPacket {
    uint8* Data;
    int32 CountBits;
    bool bError;
};
```

## Relevance to netudp

**This is the most architecturally interesting pattern from UE5 for netudp.**

netudp's layered architecture already follows this pattern (crypto → compress → reliability → fragment → socket), but UE5 makes it **pluggable at runtime**. While netudp doesn't need runtime pluggability (layers are compile-time), the concept of each layer operating on `ProcessedPacket` (data pointer + bit count + error flag) is clean and worth adopting.

**However:** netudp should NOT copy UE5's complexity. UE5's PacketHandler system is over-engineered for netudp's needs — it supports dynamic handler loading from INI files, async initialization, and UObject lifetime management. netudp's layers are fixed at compile time.
