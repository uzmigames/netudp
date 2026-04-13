## 1. Implementation

- [ ] 1.1 Add fast-path check at top of per-connection loop: `if (pending_mask == 0 && time - last_send_time < 1.0)`
- [ ] 1.2 Fast-path: only do timeout check + slow_tick check (2 comparisons, zero function calls)
- [ ] 1.3 Full-path: bandwidth.refill + budget.refill + send_pending + keepalive (existing code)
- [ ] 1.4 Ensure keepalive still fires at 1.0s even for idle peers (check in fast-path)
- [ ] 1.5 Benchmark: 5K players before/after
- [ ] 1.6 Build and verify: tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
