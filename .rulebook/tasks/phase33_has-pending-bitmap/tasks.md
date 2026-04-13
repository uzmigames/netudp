## 1. Implementation

- [x] 1.1 Add `uint8_t pending_mask` to Connection struct
- [x] 1.2 Set bit on `netudp_server_send()` after successful queue_send
- [x] 1.3 `next_channel_fast()`: checks pending_mask first, updates mask as channels drain
- [x] 1.4 Wire server_send_pending to use next_channel_fast with conn.pending_mask
- [x] 1.5 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (channel.h, connection.h comments)
- [x] 2.2 Write tests covering the new behavior (353/353 pass)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
