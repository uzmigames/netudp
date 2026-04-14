## 1. Implementation

- [x] 1.1 Define `netudp_group_t` struct: compact slot array + count + capacity + group_id (src/group/group.h)
- [x] 1.2 Add `max_groups` to `netudp_server_config_t`, allocate group array in `server_start`
- [x] 1.3 Implement `netudp_group_create` / `netudp_group_destroy` with pool-based free stack
- [x] 1.4 Implement `netudp_group_add` / `netudp_group_remove` with O(1) swap-remove and slot_to_pos map
- [x] 1.5 Implement `netudp_group_send` iterating members and calling `netudp_server_send` per member
- [x] 1.6 Implement `netudp_group_send_except` skipping one client (owner pattern)
- [x] 1.7 Implement `netudp_group_count` / `netudp_group_has` query functions
- [x] 1.8 Auto-remove client from all groups on disconnect via `server_deactivate_slot`
- [x] 1.9 Add public API declarations to `include/netudp/netudp.h`
- [x] 1.10 Build and verify all tests pass (354/354 Zig CC)

## 2. Tail (mandatory)
- [x] 2.1 Update documentation covering the implementation (CHANGELOG.md v1.3.0)
- [x] 2.2 Write tests covering the new behavior (test_groups.cpp: 8 tests)
- [x] 2.3 Run tests and confirm they pass (354/354)
