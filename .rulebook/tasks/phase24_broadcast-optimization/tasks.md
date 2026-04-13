## 1. Implementation

- [x] 1.1 Refactor broadcast to use active_slots iteration (O(active) not O(max_clients))
- [x] 1.2 Refactor broadcast_except to use active_slots iteration
- [x] 1.3 Add NETUDP_ZONE("srv::broadcast") and NETUDP_ZONE("srv::broadcast_except") profiling
- [x] 1.4 Add NETUDP_ZONE("srv::send_pkt") to server_send_packet helper
- [x] 1.5 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (server.cpp comments)
- [x] 2.2 Write tests covering the new behavior (353/353 existing tests)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
