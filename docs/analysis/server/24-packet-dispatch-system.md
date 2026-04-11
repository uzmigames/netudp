# Packet Dispatch System

**Files:** `GameServer/Shared/Networking/PacketHandler.cs` (89 lines), `GameServer/Server/Network/PacketHandler.cs` (34 lines)

### 24.1 Auto-Registration via Reflection

```csharp
// In Shared (client and server):
static PacketHandler() {
    foreach (Type t in AppDomain.CurrentDomain.GetAssemblies()
        .SelectMany(t => t.GetTypes())
        .Where(t => t.IsClass && t.Namespace == "Network.Packets")) {
        if (Activator.CreateInstance(t) is PacketHandler packetHandler)
            Handlers.Add((int)packetHandler.Type, packetHandler);
    }
}

// On Server (overrides):
static PacketHandler() {
    foreach (Type t in ... .Where(t.Namespace == "GameServer.Packets.Receive")) {
        Handlers[(int)packetHandler.Type] = packetHandler;
    }
}
```

### 24.2 Dispatch

```csharp
// Server: array of 255 slots
static readonly PacketHandler[] Handlers = new PacketHandler[255];

public static void HandlePacket(PlayerController ctrl, ByteBuffer buffer, ClientPacketType type) {
    Handlers[(int)type]?.Consume(ctrl, buffer);
}
```

### 24.3 Handler Pattern

```csharp
// Exemplo: MoveToPacketHandler.cs
public class MoveToPacketHandler : PacketHandler {
    public override ClientPacketType Type => ClientPacketType.MoveTo;

    public override void Consume(PlayerController ctrl, ByteBuffer buffer) {
        float x = buffer.GetFloat();
        float y = buffer.GetFloat();
        if (!float.IsNaN(x) && !float.IsNaN(y))
            ctrl.Player.MoveTo(new Vector2(x, y));
    }
}
```

### 24.4 Server Packets (130+ types)

Categories:
- **Entidades:** CreatureCreate, CreatureUpdate, CreatureDie, ActorDestroy (~15 types)
- **Player:** PlayerCreate, StatsChanged, ExperienceChanged, GoldChanged (~20 types)
- **Combat:** ActionCasted, DamageTaken, ProjectileCreate (~10 types)
- **UI:** OpenVendorWindow, OpenTradeWindow, TooltipInfo (~20 types)
- **Containers:** ItemAdded, ItemRemoved, QuantityChanged (~6 types)
- **Chat:** ChatMessageReceived, SystemMessage (~5 types)
- **Spells:** 30+ types de area effects (TornadoCreate, BlizzardCreate, etc)
- **Events:** BattleCastle, Champions (~5 types)
- **Trade/Party/Guild:** ~15 types

### 24.5 Client Packets (40 types)

- MoveTo, Move, Cast, AutoAttack, ChatMessage
- SetTargetEntity, CancelTarget, Interact
- UseItem, MoveItem, EquipItem, BuyItem, etc
- EntityTickUpdate (ACK de entidades unreliable)

