## 1. Implementation

- [ ] 1.1 Add `int active_slots[MAX_CLIENTS]` and `int active_count` to server struct
- [ ] 1.2 Add `int free_slots[MAX_CLIENTS]` and `int free_count` for O(1) slot allocation
- [ ] 1.3 Initialize free_slots stack with all indices on `server_start()`
- [ ] 1.4 On connect: pop from free_slots, append to active_slots
- [ ] 1.5 On disconnect/timeout: swap-remove from active_slots, push to free_slots
- [ ] 1.6 Replace all `for (i = 0; i < max_clients)` loops with active_slots iteration
- [ ] 1.7 Replace empty-slot search in connect with `free_slots[--free_count]` pop
- [ ] 1.8 Update broadcast/broadcast_except to use active_slots
- [ ] 1.9 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
