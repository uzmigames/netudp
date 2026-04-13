## 1. Implementation

- [ ] 1.1 Add `uint8_t pending_mask` to Connection struct (1 bit per channel)
- [ ] 1.2 Set bit in pending_mask on `channel.queue_send()` when message enqueued
- [ ] 1.3 Clear bit when channel send queue drains empty in `dequeue_send()`
- [ ] 1.4 Rewrite `ChannelScheduler::next_channel()` to scan pending_mask bits
- [ ] 1.5 Precompute `nagle_threshold_sec` in Channel::init() (avoid per-call division)
- [ ] 1.6 Replace `has_pending()` float comparison with cached threshold
- [ ] 1.7 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
