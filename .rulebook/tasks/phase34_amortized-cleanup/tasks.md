## 1. Implementation

- [ ] 1.1 Add `double next_frag_cleanup` to Connection, init to 0
- [ ] 1.2 Only call `frag().cleanup_timeout()` when `time >= next_frag_cleanup`, then set next = time + 1.0
- [ ] 1.3 Only call `congestion.evaluate()` when accumulated packets exceed threshold
- [ ] 1.4 Group stats/cleanup into a "slow tick" block that runs every ~100ms instead of every tick
- [ ] 1.5 Benchmark: 5000-player throughput before/after
- [ ] 1.6 Build and verify: tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
