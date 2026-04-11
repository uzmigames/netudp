# Packet Types (Wire Protocol)

**File:** `NetManager.cs` (lines 13-25)

```csharp
enum PacketType {
    Unreliable              = 0,  // Unreliable game data
    Ping                    = 1,  // Latency measurement (server → client)
    Pong                    = 2,  // Ping response (client → server)
    Ack                     = 3,  // Reliable packet acknowledgment
    Disconnected            = 4,  // Graceful disconnect
    ConnectionDenied        = 5,  // Server rejects connection
    DiffieHellmanResponseKey= 6,  // Server ECDH response
    Reliable                = 7,  // Reliable game data
    DiffieHellmanKey        = 8,  // Client ECDH request
    ConnectRequest          = 9,  // Connection request with JWT
}
```

**Total:** 10 transport-level packet types.

