## 1. Implementation

- [ ] 1.1 Add `NETUDP_SEND_STATE` flag constant to `netudp_types.h`
- [ ] 1.2 Add `uint16_t entity_id` field to `QueuedMessage` in channel.h
- [ ] 1.3 Add per-channel entity_id lookup: small hash map (entity_id -> queue index) for O(1) overwrite detection
- [ ] 1.4 Modify `Channel::queue_send` to handle `SEND_STATE` flag: check entity_id in map, overwrite if found, insert if new
- [ ] 1.5 Add `netudp_server_send_state(server, client, channel, entity_id, data, len)` public API
- [ ] 1.6 Add `netudp_group_send_state(group, channel, entity_id, data, len)` for multicast group integration
- [ ] 1.7 Clear entity_id map when channel queue is flushed in `server_send_pending`
- [ ] 1.8 Validate: state overwrite only applies to unreliable channels, return error on reliable
- [ ] 1.9 Build and verify all tests pass

## 2. Tail (mandatory)
- [ ] 2.1 Update documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
