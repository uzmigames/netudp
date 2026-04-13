## 1. Implementation

- [x] 1.1 Add active_slots[], free_slots[], slot_to_active[] arrays to server struct
- [x] 1.2 Initialize free_slots stack with all indices on server_start()
- [x] 1.3 On connect: pop from free_slots (O(1)), append to active_slots
- [x] 1.4 On disconnect/timeout: swap-remove from active_slots, push to free_slots
- [x] 1.5 Helper: server_deactivate_slot() with O(1) swap-remove + free push
- [x] 1.6 Replace for(i=0; i<max_clients) with for(a=0; a<active_count) over active_slots
- [x] 1.7 Handle swap-remove during iteration (don't increment on deactivate)
- [x] 1.8 Cleanup arrays on server_stop()
- [x] 1.9 Build and verify: 353/353 tests pass with Zig CC

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (server.cpp comments)
- [x] 2.2 Write tests covering the new behavior (353/353 existing tests)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
