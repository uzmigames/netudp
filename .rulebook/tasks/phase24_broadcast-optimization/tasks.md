## 1. Implementation

- [ ] 1.1 Refactor broadcast to use active_slots iteration (depends on phase21)
- [ ] 1.2 Pre-encode wire frame once for broadcast payload (avoid N copies of frame encoding)
- [ ] 1.3 Queue pre-encoded frame by reference into each connection's send queue
- [ ] 1.4 Encrypt per-connection during send_pending (different keys per connection)
- [ ] 1.5 Benchmark: broadcast to 100/500/1000 clients before vs after
- [ ] 1.6 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
