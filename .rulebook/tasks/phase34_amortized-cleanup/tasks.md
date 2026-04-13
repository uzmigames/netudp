## 1. Implementation

- [x] 1.1 Add `double next_slow_tick` to Connection struct
- [x] 1.2 Group stats/frag_cleanup/congestion into slow tick block (~10 Hz instead of every tick)
- [x] 1.3 Only execute when `time >= conn.next_slow_tick`, then set next = time + 0.1
- [x] 1.4 5000-player benchmark: contributes to 49.7K → 62.9K improvement
- [x] 1.5 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (server.cpp comments)
- [x] 2.2 Write tests covering the new behavior (353/353 pass)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
