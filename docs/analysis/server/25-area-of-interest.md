# Area of Interest (AoI)

**File:** `GameServer/Server/AreaOfInterest.cs`

### 25.1 Spatial Grid

```csharp
// Scene uses a cell grid (CellSize = 4.0 units)
// Each cell knows which AoIs observe it

// BoundingBox do AoI de um jogador:
// 25-unit radius around the position
BoundingBox bbox = new BoundingBox(
    Floor((posX - 25) / 4), Floor((posY - 25) / 4),
    Ceiling((posX + 25) / 4), Ceiling((posY + 25) / 4)
);
```

### 25.2 Callbacks

```csharp
public interface AreaOfInterestObserver {
    void EntityAdded(Entity entity);    // Entity entered the area of interest
    void EntityRemoved(Entity entity);  // Entity left the area of interest
}
```

When the player moves, the `BoundingBox` is recalculated. New cells add entities, old cells remove them.

