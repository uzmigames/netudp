## 1. Implementation

- [x] 1.1 Define `netudp_schema_t` struct: property array (name, type, size, offset, rep_condition, bit_index), max 64 properties (src/replication/schema.h)
- [x] 1.2 Implement `netudp_schema_create` / `netudp_schema_destroy` and typed `netudp_schema_add_*` (vec3, f32, u8, u16, i32, quat, blob)
- [x] 1.3 Define replication condition flags: `NETUDP_REP_ALL`, `REP_OWNER_ONLY`, `REP_SKIP_OWNER`, `REP_INITIAL_ONLY`, `REP_NOTIFY`, `REP_UNRELIABLE`, `REP_RELIABLE`, `REP_QUANTIZE`
- [x] 1.4 Define `netudp_entity_t` struct: schema pointer, entity_id, owner_client, group_id, property values buffer, `uint64_t dirty_mask` (src/replication/entity.h)
- [x] 1.5 Implement `netudp_entity_create` / `netudp_entity_destroy` with server-managed entity pool (16384 max)
- [x] 1.6 Implement typed setters (`netudp_entity_set_vec3`, `set_f32`, etc.) that compare old value and set dirty bit on change
- [x] 1.7 Implement typed getters (`netudp_entity_get_vec3`, `get_f32`, etc.) for read access
- [x] 1.8 Implement quantization encoders: vec3 (11+11+10 = 4 bytes), quat smallest-three (4 bytes), f32 half-float (2 bytes)
- [x] 1.9 Implement dirty serializer: encode `[entity_id:u16][dirty_mask:varint][dirty_prop_values...]` into wire buffer
- [x] 1.10 Implement dirty deserializer: decode wire buffer, apply property values
- [x] 1.11 Implement `netudp_server_replicate(server)` — iterate dirty entities, filter by rep condition per client, serialize, send via group (phase 40) with state overwrite (phase 41)
- [x] 1.12 Implement `netudp_entity_set_group(entity, group)` to bind entity to a multicast group
- [x] 1.13 Implement `netudp_entity_set_owner(entity, client_index)` for owner-based filtering
- [x] 1.14 Client-side RepNotify: REP_NOTIFY flag is set in schema, raw wire data delivered via receive queue — SDK layer deserializes and fires callbacks
- [x] 1.15 Initial-only replication: `needs_initial` flag sends full snapshot on first replicate
- [x] 1.16 Build and verify all tests pass (369/369 Zig CC)

## 2. Tail (mandatory)
- [x] 2.1 Update documentation covering the implementation (CHANGELOG.md v1.3.0)
- [x] 2.2 Write tests covering the new behavior (test_replication.cpp: 10 tests — unit + integration)
- [x] 2.3 Run tests and confirm they pass (369/369)
