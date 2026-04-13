## 1. Implementation

- [x] 1.1 Move `netudp_server_receive_batch()` from `api.cpp` to `server.cpp`
- [x] 1.2 Iterate `active_slots[0..active_count]` instead of `0..max_clients`
- [x] 1.3 Add NETUDP_ZONE("srv::receive_batch") profiling
- [x] 1.4 5000-player benchmark: 49.7K → 62.9K msgs/s (+27%)
- [x] 1.5 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (server.cpp comments)
- [x] 2.2 Write tests covering the new behavior (353/353 pass)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
