## 1. Implementation

- [ ] 1.1 Add `int pacing_slices` to `netudp_server_config_t` (default 4, 0 = burst mode)
- [ ] 1.2 Compute slice boundaries in `server_start`: divide active_count into N equal slices
- [ ] 1.3 Add `int current_pacing_slice` and `double next_slice_time` to `netudp_server`
- [ ] 1.4 Modify `server_update` send loop: only flush connections in current slice, advance slice and set next_slice_time
- [ ] 1.5 Implement sub-tick timer: use `QueryPerformanceCounter` (Windows) / `clock_gettime` (Linux) for microsecond resolution
- [ ] 1.6 Recompute slice boundaries when active_count changes (client connect/disconnect)
- [ ] 1.7 Pipeline mode integration: send thread drains send_queue in paced batches with microsecond sleeps between slices
- [ ] 1.8 Add `pacing_slices = 0` bypass for backward compatibility (sends all in one burst as before)
- [ ] 1.9 Benchmark: measure client-side jitter (inter-packet arrival variance) with and without pacing
- [ ] 1.10 Build and verify all tests pass

## 2. Tail (mandatory)
- [ ] 2.1 Update documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
