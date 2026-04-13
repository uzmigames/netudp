# Proposal: phase19_address-hashmap-dispatch

## Why

Every incoming data packet triggers an O(N) linear scan across all connection slots to find the matching address (`server.cpp:357`). With 1000 clients, that's 1000 iterations per inbound packet. At 10K pps inbound, this becomes 10M iterations/sec — estimated 15-25% of total CPU time. Replacing with a hash table makes dispatch O(1).

## What Changes

- Add `FixedHashMap<netudp_address_t, int, N>` (address → slot index) to server struct
- Insert on connect, remove on disconnect/timeout
- Replace O(N) loop in `server_dispatch_packet()` with hash lookup
- Address hash via FNV-1a on type + data + port (already have `address_hash` in address.h)

## Impact

- Affected code: `src/server.cpp` (dispatch, connect, disconnect, timeout)
- Breaking change: NO
- User benefit: 15-25% CPU reduction at 1000+ connections, O(1) packet dispatch
