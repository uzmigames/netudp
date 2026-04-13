# Proposal: phase32_receive-batch-active-slots

## Why

`netudp_server_receive_batch()` in `api.cpp` iterates `max_clients` slots (O(N)) to drain messages. With 5,000 slots, this produced **286M calls to `srv::receive`** in a 5s benchmark — the single biggest CPU consumer. The server already tracks `active_slots[]` (phase 21) but `receive_batch` in api.cpp doesn't use it because it's an opaque extern "C" function that can't access server internals.

Fix: move `receive_batch` into `server.cpp` where `active_slots` is accessible, iterate only active connections.

## What Changes

- Move `netudp_server_receive_batch()` from `api.cpp` into `server.cpp`
- Iterate `active_slots[0..active_count]` instead of `0..max_clients`
- Add NETUDP_ZONE profiling

## Impact

- Affected code: `src/api.cpp`, `src/server.cpp`
- Breaking change: NO
- User benefit: O(active) receive drain instead of O(max_clients). With 100 active / 5000 slots = 50x fewer iterations.
