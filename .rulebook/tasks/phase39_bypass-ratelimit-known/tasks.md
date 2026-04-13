## 1. Implementation

- [ ] 1.1 In recv dispatch loop: do address_to_slot lookup BEFORE rate_limiter.allow()
- [ ] 1.2 If slot found and connection active: process directly (no rate limiter)
- [ ] 1.3 If slot NOT found (unknown address): rate_limiter.allow() then connection request path
- [ ] 1.4 DDoS counter only incremented for unknown/rate-limited packets
- [ ] 1.5 Benchmark before/after
- [ ] 1.6 Build and verify: tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
