# Proposal: phase40_multicast-groups

## Why

MMORPG servers broadcast entity updates to subsets of players (zones, areas of interest). Today `netudp_server_broadcast()` iterates ALL connected clients — O(max_clients). With 5000 players and 500 entities updating per tick, that's 2.5M unnecessary checks/sends. Multicast groups pre-compute "who needs this update" and send to O(members) only. This is the foundation for all replication features (phases 41-44).

## What Changes

- New `netudp_group_t` opaque handle — compact slot array of member client indices
- API: `netudp_group_create`, `netudp_group_destroy`, `netudp_group_add`, `netudp_group_remove`, `netudp_group_count`
- API: `netudp_group_send(group, channel, data, len, flags)` — send to all members
- API: `netudp_group_send_except(group, except_client, channel, data, len, flags)` — skip owner pattern
- Internal: groups stored as compact arrays (same swap-remove pattern as active_slots)
- Internal: `group_send` iterates only members, calls `server_send_pending` per member via batch path
- Max groups configurable via `netudp_server_config_t::max_groups` (default 256)
- Client can belong to multiple groups simultaneously (zone + party + raid + guild)

## Impact

- Affected code: `src/server.cpp`, new `src/group.h`/`src/group.cpp`, public API `include/netudp/netudp.h`
- Breaking change: NO (additive API)
- User benefit: Zone broadcast O(members) instead of O(max_clients). 200 players/zone with 5000 total = 25x fewer iterations.
