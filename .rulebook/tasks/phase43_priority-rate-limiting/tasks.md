## 1. Implementation

- [ ] 1.1 Add `uint8_t priority` and `float max_rate_hz` fields to `netudp_entity_t`
- [ ] 1.2 Add `double last_replicate_time` per entity for rate limit tracking
- [ ] 1.3 Implement rate check in `netudp_server_replicate`: do not send entity if `now - last_replicate_time < 1.0/max_rate_hz`
- [ ] 1.4 Implement per-client relevancy table: `netudp_entity_set_relevancy(entity, client, priority, rate_hz)` overrides default per entity-client pair
- [ ] 1.5 Implement priority-based drop: when per-client bandwidth budget exceeded, sort pending entities by priority, drop lowest first
- [ ] 1.6 Implement starvation prevention: guarantee minimum 1 update every `min_update_interval` seconds (configurable, default 2.0s) even for low-priority entities
- [ ] 1.7 Add `netudp_entity_set_priority(entity, priority)` and `netudp_entity_set_max_rate(entity, hz)` setters
- [ ] 1.8 Integrate with `netudp_server_replicate()` from phase 42: rate check runs before dirty serialization
- [ ] 1.9 Build and verify all tests pass

## 2. Tail (mandatory)
- [ ] 2.1 Update documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
