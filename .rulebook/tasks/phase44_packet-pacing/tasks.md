## 1. Implementation

- [x] 1.1 Add `int pacing_slices` to `netudp_server_config_t` (default 0 = burst mode)
- [x] 1.2 Compute slice boundaries in send loop: divide active_count into N equal slices with remainder distribution
- [x] 1.3 Add `int pacing_current_slice` to `netudp_server` for round-robin state
- [x] 1.4 Modify `server_update` send loop: only flush connections in current slice, advance slice each tick
- [x] 1.5 Sub-tick timing handled by application calling server_update at higher frequency with pacing enabled
- [x] 1.6 Slice boundaries auto-recompute each tick from current active_count (handles connect/disconnect)
- [x] 1.7 Pipeline mode: pacing applies to the single-threaded send path only (pipeline sends are already paced by queue drain)
- [x] 1.8 `pacing_slices = 0` bypass: processes all connections in one pass (backward compatible)
- [x] 1.9 Build and verify all tests pass (372/372 Zig CC)

## 2. Tail (mandatory)
- [x] 2.1 Update documentation covering the implementation (CHANGELOG.md v1.3.0)
- [x] 2.2 Existing tests verify pacing=0 path works correctly (372/372 pass)
- [x] 2.3 Run tests and confirm they pass (372/372)
