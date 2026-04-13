## 1. Implementation

- [ ] 1.1 Add `FixedHashMap<netudp_address_t, int, 4096>` address_to_slot map to server struct
- [ ] 1.2 Insert into map on successful connection establishment
- [ ] 1.3 Remove from map on disconnect, timeout, and connection reset
- [ ] 1.4 Replace O(N) loop in `server_dispatch_packet()` with `address_to_slot.find(from)`
- [ ] 1.5 Verify address_hash function exists and is suitable (FNV-1a on type+data+port)
- [ ] 1.6 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
