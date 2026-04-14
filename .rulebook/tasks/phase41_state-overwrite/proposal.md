# Proposal: phase41_state-overwrite

## Why

In MMORPG servers running at 60Hz, multiple position updates for the same entity queue up before the previous one leaves the socket. Server enqueues frame 1, frame 2, frame 3 for NPC_42 — but only frame 3 matters. The first two waste bandwidth and crypto ops. State overwrite (latest-wins) detects pending updates for the same entity_id in the send queue and replaces them instead of enqueuing duplicates. ENet and Valve GNS both implement this pattern. Expected bandwidth reduction: 2-5x in high-density scenarios (cities, raids).

## What Changes

- New send mode: `NETUDP_SEND_STATE` flag on `netudp_server_send` / `netudp_group_send`
- New API: `netudp_server_send_state(server, client, channel, entity_id, data, len)` — convenience wrapper
- Channel queue: when `SEND_STATE` flag is set, check if an existing queued message has the same `entity_id`. If found, overwrite its payload in-place instead of enqueuing
- Entity ID is a `uint16_t` passed as the first 2 bytes of the payload (or via explicit parameter)
- Internal: `QueuedMessage` gets optional `entity_id` field for O(1) lookup via small hash map per channel
- Compatible with unreliable channels only (reliable channels must deliver all updates in order)

## Impact

- Affected code: `src/channel/channel.h`, `src/server.cpp`, `src/client.cpp`, public API
- Breaking change: NO (additive flag + new API function)
- User benefit: 2-5x fewer redundant position updates in high-density areas. 300 visible entities at 60Hz server tick with 20Hz client update = 66% of updates overwritten.
