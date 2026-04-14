## 1. Implementation

- [ ] 1.1 Define `netudp_schema_t` struct: property array (name, type, size, offset, rep_condition, bit_index), max 64 properties
- [ ] 1.2 Implement `netudp_schema_create` / `netudp_schema_destroy` and typed `netudp_schema_add_*` (vec3, f32, u8, u16, i32, quat, blob)
- [ ] 1.3 Define replication condition flags: `NETUDP_REP_ALL`, `REP_OWNER_ONLY`, `REP_SKIP_OWNER`, `REP_INITIAL_ONLY`, `REP_NOTIFY`, `REP_UNRELIABLE`, `REP_RELIABLE`
- [ ] 1.4 Define `netudp_entity_t` struct: schema pointer, entity_id, owner_client, group_id, property values buffer, `uint64_t dirty_mask`
- [ ] 1.5 Implement `netudp_entity_create` / `netudp_entity_destroy` with server-managed entity pool
- [ ] 1.6 Implement typed setters (`netudp_entity_set_vec3`, `set_f32`, etc.) that compare old value and set dirty bit on change
- [ ] 1.7 Implement typed getters (`netudp_entity_get_vec3`, `get_f32`, etc.) for read access
- [ ] 1.8 Implement quantization encoders: vec3 (10+11+10 = 4 bytes), quat smallest-three (4 bytes), f32 half-float (2 bytes)
- [ ] 1.9 Implement dirty serializer: encode `[entity_id:u16][dirty_mask:varint][dirty_prop_values...]` into wire buffer
- [ ] 1.10 Implement dirty deserializer (client-side): decode wire buffer, apply property values, fire RepNotify callbacks
- [ ] 1.11 Implement `netudp_server_replicate(server)` — iterate dirty entities, filter by rep condition per client, serialize, send via group (phase 40) with state overwrite (phase 41)
- [ ] 1.12 Implement `netudp_entity_set_group(entity, group)` to bind entity to a multicast group
- [ ] 1.13 Implement `netudp_entity_set_owner(entity, client_index)` for owner-based filtering
- [ ] 1.14 Implement client-side `netudp_client_on_property_change(client, schema, prop_name, callback)` for RepNotify
- [ ] 1.15 Implement initial-only replication: full property snapshot sent once on entity spawn/client join
- [ ] 1.16 Build and verify all tests pass

## 2. Tail (mandatory)
- [ ] 2.1 Update documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
