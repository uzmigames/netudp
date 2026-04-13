# Proposal: phase24_broadcast-optimization

## Why

`netudp_server_broadcast()` calls `netudp_server_send()` in a loop for each active client — each call copies the message into the channel queue individually. For 1000 players, that's 1000 memcpy + 1000 queue_send operations for the same data. The payload is identical for all clients; only the encryption (per-connection key) differs. We can share the plaintext frame and only encrypt per-connection, or batch the queue operations.

Also, broadcast iterates all max_clients slots (O(N)) instead of just active connections (fixed by phase21).

## What Changes

- Optimize broadcast to queue the frame once and reference-count the payload
- Use active_slots list (from phase21) instead of full slot scan
- Pre-encode the wire frame once, then encrypt per-connection during send_pending
- For unreliable broadcasts: consider shared encrypted packet if all connections use same epoch

## Impact

- Affected code: `src/server.cpp` (broadcast, broadcast_except)
- Breaking change: NO
- User benefit: O(active) instead of O(max_clients), reduced memcpy for large broadcasts
