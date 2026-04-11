# Reliable vs Unreliable Entities

### 26.1 Reliable Entities (ReliableEntity)

**File:** `GameServer/Server/Entities/ReliableEntity.cs`

```csharp
public abstract class ReliableEntity : Entity {
    public override void OnEnterAreaOfInterest(PlayerController ctrl) {
        ctrl.ReliableEntites.Add(Id, this);
        WriteCreatePacket(ctrl);  // Reliable: guaranteed
    }

    public override void OnLeftAreaOfInterest(PlayerController ctrl) {
        ctrl.ReliableEntites.Remove(Id);
        WriteDestroyPacket(ctrl);  // Reliable: guaranteed
        // [ServerPacketType.ActorDestroyReliable][int32 id][byte 0]
    }
}
```

Used for: blueprints, houses, resources, static NPCs.

### 26.2 Unreliable Entities (AutoDestroyEntity)

```csharp
public class AutoDestroyEntity<T> : Entity {
    public override void OnEnterAreaOfInterest(PlayerController ctrl) {
        WriteCreatePacket(ctrl, Scene.Manager.TickNumber);
        // Unreliable: may be lost, but the tick allows
        // the client to know if it is stale
    }

    public override void OnLeftAreaOfInterest(PlayerController ctrl) {
        // Nothing! The destroy is inferred from the EntityTickUpdate ACK
    }
}
```

Used for: creatures, players, projectiles, effects.

### 26.3 Tracking per PlayerController

```csharp
// PlayerController.cs
QuickDictionary<Entry> UnreliableEntities;  // Active unreliable entities
Dictionary<int, Entity> ReliableEntites;     // Active reliable entities

struct Entry {
    Entity Entity;
    uint CreatedAt;      // Creation tick
    uint DestroyedAt;    // Destruction tick (0 = active)
    uint LastAckTick;    // Last tick acknowledged by the client
    int DestroyBackoff;
}
```

