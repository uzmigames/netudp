## 1. Implementation

- [ ] 1.1 Add `uint8_t pending_mask` to Connection (1 bit per channel, up to 8)
- [ ] 1.2 Set bit in `channel.queue_send()` when message enqueued
- [ ] 1.3 Clear bit in `channel.dequeue_send()` when queue becomes empty
- [ ] 1.4 `ChannelScheduler::next_channel()`: early return -1 if pending_mask == 0
- [ ] 1.5 Find best channel via masked priority scan instead of calling has_pending N times
- [ ] 1.6 Benchmark: 5000-player throughput before/after
- [ ] 1.7 Build and verify: tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
