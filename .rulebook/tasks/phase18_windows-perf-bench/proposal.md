# Proposal: phase18_windows-perf-bench

## Why

After implementing RIO and WFP tuning, we need dedicated benchmarks to measure the actual Windows PPS improvement across all socket backends (sendto loop, WSASendTo batch, RIO polled). Current benchmarks only measure the WSASendTo path. We need to compare backends head-to-head, measure frame coalescing impact with multi-msg workloads, and produce a definitive comparison table for the performance docs.

## What Changes

- New benchmark: `bench_socket_backends.cpp` — measures raw PPS per backend (sendto, WSASendTo, RIO) with same workload
- New benchmark: `bench_coalescing.cpp` — sends N small messages per tick and measures PPS with vs without coalescing
- Update `netudp_bench` to include new benchmarks in the standard run
- Update `docs/analysis/performance/01-current-state.md` with measured RIO numbers
- Update `README.md` performance table with Windows backend comparison

## Impact

- Affected specs: benchmark suite
- Affected code: new `bench/bench_socket_backends.cpp`, `bench/bench_coalescing.cpp`, `bench/CMakeLists.txt`
- Breaking change: NO
- User benefit: Documented proof of which backend to use, informed deployment decisions
