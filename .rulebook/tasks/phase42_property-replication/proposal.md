# Proposal: phase42_property-replication

## Why

MMORPG entity replication requires sending only changed properties per entity, with conditional filtering (owner-only, skip-owner, initial-only) and client-side change notifications (RepNotify). Today the application must manually serialize, track dirty state, and filter per-client — duplicating logic that every game server needs. Unreal Engine's `UPROPERTY(Replicated)` system proves this pattern at scale. Building it into netudp's core eliminates boilerplate, enables automatic dirty tracking with bitmask delta encoding, and reduces per-entity bandwidth by 70-80% (sending 12 bytes of dirty props instead of 48 bytes of full state).

## What Changes

- **Schema system**: `netudp_schema_t` — defines property layout for an entity type (vec3, f32, u8, u16, i32, quat, blob). Each property has a name, type, size, replication condition, and bit index in the dirty mask.
- **Entity handle**: `netudp_entity_t` — per-entity instance bound to a schema. Holds current property values + dirty bitmask (up to 64 properties per schema via `uint64_t` mask).
- **Typed setters**: `netudp_entity_set_vec3`, `set_f32`, `set_u8`, etc. — automatically mark the property's dirty bit on change.
- **Replication conditions**: `NETUDP_REP_ALL`, `REP_OWNER_ONLY`, `REP_SKIP_OWNER`, `REP_INITIAL_ONLY`, `REP_NOTIFY`, `REP_UNRELIABLE`, `REP_RELIABLE`.
- **Server replicate**: `netudp_server_replicate(server)` — iterates all dirty entities, serializes only dirty properties per replication condition, sends to the entity's multicast group (phase 40). Uses state overwrite (phase 41) to merge dirty masks for pending updates.
- **Wire format**: `[entity_id:u16][dirty_mask:varint][prop_values...]` — compact, only dirty props serialized.
- **Client RepNotify**: `netudp_client_on_property_change(client, schema, prop_name, callback)` — fires when a replicated property arrives with a new value.
- **Quantization**: built-in vec3 quantization (10+11+10 bit = 4 bytes), quat smallest-three (29 bits), f32 half-float — configurable per property.

## Impact

- Affected code: new `src/replication/schema.h`, `src/replication/entity.h`, `src/replication/replicate.cpp`, public API
- Dependencies: phase 40 (multicast groups), phase 41 (state overwrite)
- Breaking change: NO (entirely additive API)
- User benefit: 70-80% bandwidth reduction per entity. Automatic dirty tracking. Conditional replication. RepNotify callbacks. Eliminates manual serialization boilerplate.
