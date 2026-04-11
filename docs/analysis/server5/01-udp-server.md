# 1. UDPServer — Main Server

**File:** `Core/Network/UDPServer.cs` (1219 lines)

## Architecture

Same dual-thread model as Server1, but with modern .NET patterns:

```
┌─────────────────────────────────────────────┐
│ Receive Thread                               │
│  - UDP.Poll(timeout) + batch recv            │
│  - WAF rate limit check (token bucket)       │
│  - Cookie validation (anti-spoof)            │
│  - CRC32C check OR PacketHeader decrypt      │
│  - Route to UDPSocket event queue (Channel)  │
├─────────────────────────────────────────────┤
│ Send Thread                                  │
│  - Drain SendQueue (Channel<SendPacket>)     │
│  - UDP.Unsafe.Send()                         │
│  - StructPool buffer management              │
├─────────────────────────────────────────────┤
│ Game Thread(s)                               │
│  - UDPSocket.ProcessPacket() per connection  │
│  - Reliable + Unreliable queues (separate)   │
└─────────────────────────────────────────────┘
```

## Key Data Structures

```csharp
public unsafe struct SendPacket {
    public byte* Buffer;
    public int Length;
    public Address Address;
    public bool Pooled;
}

public readonly struct ReceivedPacket {
    public readonly FlatBuffer Buffer;
    public readonly Address Address;
}

public class UDPServerOptions {
    public uint Version { get; set; } = 1;
    public bool EnableWAF { get; set; } = true;
    public bool EnableIntegrityCheck { get; set; } = true;
    public int ReceiveTimeout { get; set; } = 10;
    public bool UseXOREncode { get; set; } = false;
}
```

## Connection Flow (New: Cookie-Based)

```
Client                              Server
  │                                    │
  │  1. Connect (any packet)           │
  │ ──────────────────────────────────→│
  │                                    │  WAF check (token bucket)
  │  2. Cookie                         │  Generate HMAC-SHA256 cookie
  │ ←──────────────────────────────────│
  │                                    │
  │  3. Connect + Cookie               │
  │ ──────────────────────────────────→│  Validate cookie (stateless)
  │                                    │  Create UDPSocket
  │                                    │  X25519 key exchange
  │  4. ConnectionAccepted             │
  │     [id, server_pubkey, salt]      │
  │ ←──────────────────────────────────│
  │                                    │
  │  Client derives ChaCha20 keys     │
  │                                    │
  │  5. CryptoTest (encrypted)        │
  │ ──────────────────────────────────→│  Decrypt with session keys
  │                                    │
  │  6. CryptoTestAck (encrypted)     │
  │ ←──────────────────────────────────│
  │                                    │
  │  ALL subsequent traffic encrypted  │
```

## Key Constants

```csharp
public const int Mtu = 1200;
public static TimeSpan ReliableTimeout = TimeSpan.FromMilliseconds(250);
public const int MaxConnections = 10000;   // vs 255 in Server1!
```

## Connection Storage

```csharp
public static ConcurrentDictionary<uint, UDPSocket> Clients;     // By connection ID
private static ConcurrentDictionary<Address, UDPSocket> Addresses; // By address
```

Uses `ConcurrentDictionary` instead of plain `Dictionary` — thread-safe without manual locking.

## Send Queue

```csharp
private static Channel<SendPacket> SendQueue = Channel.CreateBounded<SendPacket>(
    new BoundedChannelOptions(100000) {
        SingleReader = true,
        SingleWriter = false,
        FullMode = BoundedChannelFullMode.DropOldest
    });
```

Uses .NET `Channel<T>` instead of custom lock-free queues. Bounded at 100K with DropOldest backpressure.

## Statistics

```csharp
internal static long _packetsReceived;
internal static long _bytesSent;
internal static long _bytesReceived;
internal static long _packetsSent;
// Atomically updated with Interlocked.Increment/Add
```
