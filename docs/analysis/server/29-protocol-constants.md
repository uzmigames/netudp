# Protocol Constants

**File:** `GameServer/Shared/Networking/NetConstants.cs`

```csharp
public static class NetConstants {
    const ushort MaxSequence     = 8192;
    const ushort HalfMaxSequence = 4096;
    const int WindowSize         = 16384;  // MaxSequence * 2
    const int HalfWindowSize     = 8192;
    const int ChunkSize          = 32;     // Game units per chunk
    const int ChunkSizeLog       = 5;      // log2(32)
    const int PacketPoolSize     = 1000;
    const int MaxUdpHeaderSize   = 68;     // IP + UDP headers
    const int ProtocolId         = 13;     // Protocol identifier
}

// Connection.cs
const int Mtu = 1200;  // Maximum Transmission Unit

// NetworkGeneral.cs (alternative version/legada)
const int MaxGameSequence     = 32000;
const int HalfMaxGameSequence = 16000;
const int ProtocolId          = 1;
```

