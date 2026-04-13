## 1. Implementation

- [ ] 1.1 Move `netudp_server_receive_batch()` from `api.cpp` to `server.cpp`
- [ ] 1.2 Iterate `active_slots[0..active_count]` instead of `0..max_clients`
- [ ] 1.3 Add NETUDP_ZONE("srv::receive_batch") profiling
- [ ] 1.4 Benchmark: 5000-player throughput before/after
- [ ] 1.5 Build and verify: tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
