## 1. Implementation

- [ ] 1.1 Define `netudp_group_t` struct: compact slot array + count + capacity + group_id
- [ ] 1.2 Add `max_groups` to `netudp_server_config_t`, allocate group array in `server_start`
- [ ] 1.3 Implement `netudp_group_create` / `netudp_group_destroy` with pool-based allocation
- [ ] 1.4 Implement `netudp_group_add` / `netudp_group_remove` with O(1) swap-remove and slot_to_pos map
- [ ] 1.5 Implement `netudp_group_send` iterating members and calling `netudp_server_send` per member
- [ ] 1.6 Implement `netudp_group_send_except` skipping one client (owner pattern)
- [ ] 1.7 Implement `netudp_group_count` / `netudp_group_has` query functions
- [ ] 1.8 Auto-remove client from all groups on disconnect via disconnect path hook
- [ ] 1.9 Add public API declarations to `include/netudp/netudp.h`
- [ ] 1.10 Build and verify all tests pass

## 2. Tail (mandatory)
- [ ] 2.1 Update documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
