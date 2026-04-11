# 4. Packet System

**Files:** `src/engine/network/packet.ts`, `src/engine/network/*.packet.ts`

## Base Packet Class

```typescript
export class Packet {
    static packetsServer: Map<ServerPacketType, Packet> = new Map();
    static packetsClient: Map<ClientPacketType, Packet> = new Map();
    
    public type: ServerPacketType | ClientPacketType;

    public send(entity: Entity, data: any = null) {
        if (entity.socket)
            entity.socket.send(
                new ByteBuffer()
                    .putByte(this.type)
                    .putString(JSON.stringify(data))  // JSON serialization!
                    .getBuffer()
            );
    }
}
```

## Serialization: Binary Header + JSON Body

The default packet format is **1 byte type + JSON string**:

```
[1 byte: packet type] [4 bytes: string length] [N bytes: JSON UTF-8 string]
```

This is a hybrid approach — binary framing with JSON payloads. Simple to develop but more bandwidth than pure binary.

Some performance-critical packets (entity updates, auto-attack) use pure binary serialization with ByteBuffer instead of JSON.

## Packet Registration

Packets are singleton instances registered in static maps:

```typescript
export let packetPong = new PacketPong();
export let packetLogin = new PacketLogin();
// etc.
```

## Game-Specific (NOT relevant to netudp)

The packet types (72 server types, 63 client types) are entirely game-specific. netudp provides raw channels — the application defines its own packet types.
