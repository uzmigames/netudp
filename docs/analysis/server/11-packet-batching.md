# Packet Batching System

**Files:** `Connection.cs` (BeginReliable/EndReliable, BeginUnreliable/EndUnreliable)

**This is the most critical performance pattern in the server.**

```csharp
// Typical usage in game code (PlayerController.cs):
using (var buffer = Connection.BeginReliable()) {
    buffer.Put(ServerPacketType.PlayerGoldCoinsChanged);
    buffer.PutVar(Player.GoldCoins);
}
// The `using` calls Dispose() → EndReliable()

using (var buffer = Connection.BeginReliable()) {
    buffer.Put(ServerPacketType.SkillExperienceChanged);
    buffer.Put((byte)name);
    buffer.PutVarPositive(value);
}
// Same buffer! Multiple messages in the same UDP packet
```

**How it works:**
1. `BeginReliable()` returns the **current** buffer (or creates a new if null)
2. Each call appends data to the same buffer
3. `EndReliable()` checks: if `Position >= Mtu (1200)`, flush and create a new buffer
4. In the tick's `Update()`, any pending buffer is sent

**Result:** Dozens of game messages packed into 1-2 UDP packets per tick, instead of 1 packet per message. This drastically reduces UDP header (28 bytes) and CRC (4 bytes) overhead.

```
WITHOUT batching:                 WITH batching:
[UDP][type][msg1][CRC]        [UDP][type][seq][tick][msg1][msg2]...[msgN][CRC]
[UDP][type][msg2][CRC]        → 1 UDP packet for N messages
[UDP][type][msg3][CRC]
→ N UDP packets for N messages
```

