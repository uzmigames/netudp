# 2. UDPSocket — Connection State

**File:** `Core/Network/UDPSocket.cs` (641 lines)

## Per-Connection State

```csharp
public class UDPSocket {
    public uint Id;                          // Connection ID (uint32, not byte!)
    public uint EntityId;                    // Linked game entity
    public Address RemoteAddress;
    public ConnectionState State;            // Disconnected/Connecting/Connected/Disconnecting
    public float TimeoutLeft = 30f;          // 30s (vs 120s in Server1)
    public uint Ping = 0;
    public SecureSession Session;            // X25519 + ChaCha20

    // Crypto handshake
    public bool ClientCryptoConfirmed;
    public bool ServerCryptoConfirmed;
    public bool CryptoHandshakeComplete => ClientCryptoConfirmed && ServerCryptoConfirmed;

    // Integrity check
    public bool EnableIntegrityCheck = true;
    public float TimeoutIntegrityCheck = 120f;
}
```

## Separate Queues (NEW)

```csharp
public Channel<FlatBuffer> ReliableEventQueue;    // Ordered processing
public Channel<FlatBuffer> UnreliableEventQueue;  // Unordered processing
```

## RTT-Adaptive Retransmission (NEW)

```csharp
float resendMs = Math.Max(Ping, (uint)UDPServer.ReliableTimeout.TotalMilliseconds);
// Max 10 retries (vs 30 in Server1)
```

## Fragmentation (NEW)

```csharp
private ushort NextFragmentId = 1;
internal ConcurrentDictionary<ushort, FragmentInfo> Fragments;
// Cleanup fragments after timeout
```

## Encrypted Send Path

Serialize → Build PacketHeader → Get AAD → ChaCha20 encrypt → Optional LZ4 → Send
