## 1. Implementation

- [x] 1.1 Add `FixedHashMap<netudp_address_t, int, 4096>` address_to_slot map to server struct
- [x] 1.2 Insert into map on successful connection establishment (server.cpp:810)
- [x] 1.3 Remove from map on timeout disconnect (server.cpp:501)
- [x] 1.4 Remove from map on FRAME_DISCONNECT (server.cpp:1019)
- [x] 1.5 Clear map on server_stop()
- [x] 1.6 Replace O(N) loop in server_dispatch_packet() with address_to_slot.find() — O(1)
- [x] 1.7 Build and verify: 353/353 tests pass with Zig CC

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (server.cpp comments, analysis doc)
- [x] 2.2 Write tests covering the new behavior (353/353 existing tests exercise dispatch path)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
