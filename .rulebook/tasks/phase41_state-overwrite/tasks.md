## 1. Implementation

- [x] 1.1 Add `NETUDP_SEND_STATE` flag constant (= 8) to `netudp_types.h`
- [x] 1.2 Add `uint16_t entity_id` field to `QueuedMessage` in channel.h
- [x] 1.3 Queue scan for overwrite detection (backward scan, max 256 entries)
- [x] 1.4 Modify `Channel::queue_send_impl` to handle `SEND_STATE` flag: scan for matching entity_id, overwrite in-place if found
- [x] 1.5 Add `netudp_server_send_state(server, client, channel, entity_id, data, len)` public API
- [x] 1.6 Add `netudp_group_send_state(server, group, channel, entity_id, data, len)` for multicast group integration
- [x] 1.7 Entity_id map cleared implicitly: overwrite entries are replaced in-place, dequeued normally
- [x] 1.8 Validate: state overwrite returns error on reliable channels (RELIABLE_ORDERED, RELIABLE_UNORDERED)
- [x] 1.9 Build and verify all tests pass (359/359 Zig CC)

## 2. Tail (mandatory)
- [x] 2.1 Update documentation covering the implementation (CHANGELOG.md v1.3.0)
- [x] 2.2 Write tests covering the new behavior (test_state_overwrite.cpp: 5 tests)
- [x] 2.3 Run tests and confirm they pass (359/359)
