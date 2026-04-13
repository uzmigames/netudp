## 1. Implementation

- [ ] 1.1 Design ConnectionWorker struct: owns a slice of active_slots, has per-worker recv queue
- [ ] 1.2 Implement connection-to-worker routing via address hash (deterministic, consistent)
- [ ] 1.3 Each worker runs its own per-tick loop: bandwidth refill, send_pending, keepalive, timeout
- [ ] 1.4 Recv thread dispatches packets to per-worker queues based on connection→worker mapping
- [ ] 1.5 Game thread (`update()`) signals all workers, waits for all to complete (barrier sync)
- [ ] 1.6 Connect/disconnect handled by game thread (workers don't modify the active list)
- [ ] 1.7 Add `num_workers` to server config (default 1 = single-thread)
- [ ] 1.8 Ensure per-worker send uses separate send buffers (no shared state)
- [ ] 1.9 Benchmark: 5000-player throughput with 1 vs 2 vs 4 workers
- [ ] 1.10 Build and verify: tests pass in single-worker mode

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
