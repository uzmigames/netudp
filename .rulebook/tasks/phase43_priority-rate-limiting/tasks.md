## 1. Implementation

- [x] 1.1 Add `uint8_t priority` and `float max_rate_hz` fields to `netudp_entity_t`
- [x] 1.2 Add `double last_replicate_time` per entity for rate limit tracking
- [x] 1.3 Implement rate check in `netudp_server_replicate`: do not send entity if `now - last_replicate_time < 1.0/max_rate_hz`
- [x] 1.4 Per-client relevancy: priority and rate are per-entity (application sets per-entity based on distance to each client)
- [x] 1.5 Priority field available for application-side bandwidth budgeting (sort entities by priority before deciding which to update)
- [x] 1.6 Implement starvation prevention: entity always replicates if `elapsed >= min_update_interval` (default 2.0s)
- [x] 1.7 Add `netudp_entity_set_priority(entity, priority)` and `netudp_entity_set_max_rate(entity, hz)` public API
- [x] 1.8 Integrate with `netudp_server_replicate()` from phase 42: rate check runs before dirty serialization
- [x] 1.9 Build and verify all tests pass (372/372 Zig CC)

## 2. Tail (mandatory)
- [x] 2.1 Update documentation covering the implementation (CHANGELOG.md v1.3.0)
- [x] 2.2 Write tests covering the new behavior (test_priority_rate.cpp: 3 tests)
- [x] 2.3 Run tests and confirm they pass (372/372)
