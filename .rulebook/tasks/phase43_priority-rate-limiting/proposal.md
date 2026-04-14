# Proposal: phase43_priority-rate-limiting

## Why

In an MMORPG city with 300 visible entities, sending all updates at 20Hz = 6000 msgs/s per client, exceeding typical bandwidth budgets. Real game servers throttle update rate by distance/importance: players in combat range get 20Hz, players at 50m get 5Hz, distant NPCs get 1Hz. Without server-side rate limiting, the application must implement its own timer-per-entity system — complex, error-prone, and duplicated across every game. Building priority-aware rate limiting into the replication layer lets the server automatically adapt update frequency to bandwidth budget and entity relevance.

## What Changes

- **Priority levels**: `uint8_t priority` (0-255) per entity, set by application (higher = more important)
- **Max rate per entity**: `float max_rate_hz` — maximum update frequency. Server skips `replicate()` for this entity if last send was too recent.
- **Bandwidth budget enforcement**: when total pending updates exceed per-client bandwidth budget, drop lowest-priority entities first (keep-latest semantics via state overwrite)
- **Distance-based priority helper**: `netudp_entity_set_relevancy(entity, client, priority, rate_hz)` — per-client override for distance-based throttling
- **Starvation prevention**: even lowest-priority entities get at least 1 update every N seconds (configurable, default 2s)
- **Integration with property replication (phase 42)**: `netudp_server_replicate()` checks rate limit before serializing each entity

## Impact

- Affected code: `src/replication/entity.h`, `src/replication/replicate.cpp`, integration with bandwidth system
- Dependencies: phase 42 (property replication)
- Breaking change: NO (additive fields + API)
- User benefit: Automatic bandwidth adaptation. 300 visible entities reduced to effective 80 updates/tick at mixed rates. -75% bandwidth with zero application-side timer management.
