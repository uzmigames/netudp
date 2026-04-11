# 8. Channel System & Multiplexing

## Built-in Channel Types

| Type | Index | Purpose | Reliability |
|---|---|---|---|
| Control | 0 | Connection management, NMT_* messages | Always reliable |
| Actor | dynamic | Per-replicated-actor (RPCs, property updates) | Mixed per-bunch |
| Voice | 1 | VoIP data | Unreliable |
| File | dynamic | Binary file transfer | Configurable |

## Channel Design

- Channel index: 3 bits (0-7) — **only 8 channels per connection**
- Each channel has independent reliable sequence numbering (max 1024)
- Control channel always open, never dormant
- Actor channels created/destroyed dynamically per replicated actor
- Channel close with reason enum

## netudp comparison

| Aspect | UE5 | netudp |
|---|---|---|
| Max channels | 8 (3-bit index) | Up to 255 (8-bit) |
| Channel types | Control, Actor, Voice, File | Unreliable, UnreliableSeq, ReliableOrdered, ReliableUnordered |
| Scheduling | No priority system | Priority + weight (from GNS) |
| Nagle | No | Yes, configurable per channel |
| Dynamic creation | Yes (actor channels) | No (fixed at connection time) |
