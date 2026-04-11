# Connection Management

**File:** `GameServer/Shared/Networking/Connection.cs` (494 lines)

### 3.1 State Machine

```
Connecting (0) ──→ Connected (1) ──→ Disconnected (2)
     │                                      ↑
     └──────────── timeout ─────────────────┘
```

Three states only. Simple and effective for games.

### 3.2 Connection Storage

```csharp
// NetManager.cs — armazenamento global
static Dictionary<Address, Connection> Connections;              // Active connections
static Dictionary<Address, Connection> DiffieHellmanConnections; // Handshake in progress

// Intrusive linked list per NetManager (per Scene/map)
Connection First;            // Head of per-map connection list
Connection PendingConnections; // New connections waiting to be processed
```

**Connection ID:** `byte ConnectionId` (0-255) with static pool — severe limitation.

### 3.3 Timeout

```csharp
public float TimeoutLeft = 120f;  // 120 seconds initial

// Em ProcessPacket():
TimeoutLeft = Math.Max(15.0f, TimeoutLeft);  // Any packet resets to min 15s

// Em Update():
TimeoutLeft -= delta;
if (TimeoutLeft <= 0) Disconnect(DisconnectReason.Timeout);
```

### 3.4 Disconnect Reasons

```csharp
enum DisconnectReason {
    Timeout,             // No packets for 120s
    NegativeSequence,    // Invalid sequence (commented out in code)
    RemoteBufferTooBig,  // Reorder buffer > 200 packets
    Other                // Generic
}
```

### 3.5 Connection Fields

```csharp
public partial class Connection {
    ByteBuffer ReliableBuffer;           // Current reliable packet buffer being assembled
    ByteBuffer UnreliableBuffer;         // Current unreliable packet buffer
    ByteBuffer AckBuffer;                // Pending ACKs buffer

    Address RemoteEndPoint;              // Remote address
    NetManager Manager;                  // Owner NetManager (Scene)
    string Token;                        // JWT authentication token

    Dictionary<short, ByteBuffer> ReliablePackets;          // Sent reliable packets waiting for ACK
    Dictionary<short, ByteBuffer> RemoteReliableOrderBuffer; // Out-of-order packets waiting

    short Sequence = 1;                  // Local sequence number
    short NextRemoteSequence = 2;        // Next expected remote sequence
    int[] Window;                        // Sliding window (bitmask)

    float TimeoutLeft = 120f;            // Timeout in seconds
    int Ping = 50;                       // Latency in ms (default 50)
    const int Mtu = 1200;               // MTU hardcoded
    byte ConnectionId;                   // Connection ID (0-255)
    Connection Next;                     // Intrusive linked list

    // String interning
    QuickStringDictionary<uint> SymbolToIndex;
    QuickBag<String> IndexToSymbol;
    QuickStringDictionary<uint> SymbolToIndexRemote;
    QuickBag<String> IndexToSymbolRemote;
    uint SymbolPool;

    // Encryption
    byte[] EncryptionKey = new byte[16]; // AES-128 key
    AesGcm AesEncryptor;                 // Hardware AES-GCM

    IHeaderWriter HeaderWriter;          // Custom header per connection type
    Action Disconnected;                 // Disconnect callback
    Action<ByteBuffer> PacketReceived;   // Packet received callback
    ConnectionState State;
}
```

