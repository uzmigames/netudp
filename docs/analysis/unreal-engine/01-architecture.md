# 1. Architecture: NetDriver → NetConnection → Channel → Bunch → Packet

## Layered Hierarchy

```
NetDriver (abstract transport — can swap UDP/WebSocket/Steam/etc.)
  └── NetConnection (per-peer state machine)
        └── Channel[] (logical message streams, 0-7)
              └── Bunch (atomic message with reliability flags)
                    └── Packet (1+ bunches + header + ack history)
                          └── PacketHandler pipeline (encrypt, compress, etc.)
                                └── Socket.SendTo()
```

## NetDriver — Transport Abstraction

`UNetDriver` is the abstract base. `UIpNetDriver` implements UDP. Other drivers exist for WebSocket, Steam, etc.

```cpp
class UNetDriver : public UObject {
    UNetConnection* ServerConnection;           // Client-side: connection to server
    TArray<UNetConnection*> ClientConnections;   // Server-side: all client connections
    TUniquePtr<PacketHandler> ConnectionlessHandler; // For pre-connection packets
    int32 MaxChannelsOverride;                   // Max channels per connection
};
```

**Key insight for netudp:** The driver abstraction allows swapping the entire transport without changing game code. netudp's `extern "C"` API achieves the same — engines bind once, transport is interchangeable.

## NetConnection — Per-Peer State

```cpp
class UNetConnection {
    EConnectionState State;  // Invalid, Pending, Open, Closing, Closed
    int32 MaxPacket;         // Max packet size (default: MTU - overhead)
    int32 PacketOverhead;    // Per-packet overhead bytes
    FBitWriter SendBuffer;   // Queued bits waiting to flush
    int32 InPacketId;        // Last received packet sequence
    int32 OutPacketId;       // Last sent packet sequence
    int32 OutAckPacketId;    // Last acked sent packet
    TArray<int32> OutReliable; // Per-channel: last reliable sequence sent
    TArray<int32> InReliable;  // Per-channel: last reliable sequence received
    double LastReceiveTime;
    double LastSendTime;
    int32 QueuedBits;        // Bandwidth token bucket
};
```

## Data Flow

```
Game RPC call
  → Actor Channel serializes RPC into Bunch
  → Bunch flags: bReliable, bPartial, channel index
  → NetConnection packs Bunch into SendBuffer (FBitWriter)
  → FlushNet() called at end of tick
  → PacketHandler pipeline: encrypt, compress
  → Socket.SendTo()

Incoming packet:
  → Socket.RecvFrom()
  → PacketHandler pipeline (reverse): decrypt, decompress
  → NetConnection::ReceivedRawPacket()
  → Disassemble header (sequence, acks)
  → Extract bunches, dispatch to channels by index
  → Channel processes bunch (deliver RPC, update properties)
```
