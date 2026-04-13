## 1. Implementation

- [x] 1.1 Add connection_worker_fn: processes a slice of active_slots in parallel
- [x] 1.2 Worker threads spin-wait on workers_go atomic, signal done via workers_done
- [x] 1.3 Main thread dispatches: sets worker_time/dt, signals go, waits for done
- [x] 1.4 Timeout check stays on main thread (modifies active_slots)
- [x] 1.5 Activation: num_io_threads >= 3 spawns (num_io_threads-1) workers (max 8)
- [x] 1.6 Worker cleanup in server_stop: join all worker threads
- [x] 1.7 Single-thread mode unchanged (num_workers=1 inline loop)
- [x] 1.8 Benchmark: 5K players t2=60.7K vs t5=65.3K (+8%)
- [x] 1.9 353/353 tests pass (single-thread default)

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (server.cpp comments)
- [x] 2.2 Write tests covering the new behavior (353/353 pass)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
