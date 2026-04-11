# Entity Tick Acknowledgment

**File:** `GameServer/Server/Network/Packets/Receive/Core/EntityTickUpdatePacketHandler.cs`

### 27.1 Mechanism

The client sends `EntityTickUpdate` periodically to confirm it "saw" an entity:

```csharp
// Cliente → Servidor
buffer.Put(ClientPacketType.EntityTickUpdate);
buffer.Put(entityId);       // int32
buffer.Put(tickNumber);     // uint32 — last tick the client processed

// Server processes:
if (entry.LastAckTick < entityTickNumber) {
    // If the entity was already destroyed AND the client has seen the destruction tick
    if (entry.DestroyedAt >= entry.CreatedAt &&
        entry.LastAckTick >= entry.CreatedAt &&
        entityTickNumber >= entry.DestroyedAt) {
        // Remove completely — client knows it no longer exists
        ctrl.UnreliableEntities.FastRemove(id);
    }
    entry.LastAckTick = entityTickNumber;
}
```

### 27.2 Why does this exist?

Entidades unreliable nao tem `ActorDestroy` reliable. O servidor precisa saber when o cliente ja "viu" o estado de destruicao para parar de enviar updates. O EntityTickUpdate e o mecanismo de confirmacao implicit.

