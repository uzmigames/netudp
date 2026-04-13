## 1. Implementation

- [ ] 1.1 Design lock-free SPSC packet queue: `RecvPacketQueue` (recv thread → game thread)
- [ ] 1.2 Design lock-free SPSC send queue: `SendPacketQueue` (game thread → send thread)
- [ ] 1.3 Implement recv thread: tight loop calling `socket_recv_batch`, push into RecvPacketQueue
- [ ] 1.4 Implement send thread: drain SendPacketQueue, call `socket_send_batch` (coalesced)
- [ ] 1.5 Refactor `netudp_server_update()`: drain RecvPacketQueue → dispatch → process → enqueue to SendPacketQueue
- [ ] 1.6 Add `threading_mode` to server config: SINGLE (default), PIPELINE (recv+send threads)
- [ ] 1.7 Thread lifecycle: start on `server_start()`, join on `server_stop()`
- [ ] 1.8 Ensure single-threaded mode is unchanged (backward compat)
- [ ] 1.9 Benchmark: single-thread vs pipeline mode PPS comparison
- [ ] 1.10 Build and verify: tests pass in both modes

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
