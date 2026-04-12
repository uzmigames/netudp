# Proposal: phase10_profiling-audit

## Why

Each `NETUDP_ZONE` call does a lock-free `rdtsc` + atomic add (~5-15ns). With 33 zones and 2M pps target, overhead reaches 660ms/s per core (6.6% CPU). Need to measure actual overhead and ensure `NETUDP_ZONE` compiles to a no-op when profiling is disabled, so production builds pay zero cost.

Source: `docs/analysis/performance/07-optimization-roadmap.md` (Phase F)

## What Changes

- Benchmark PPS with `NETUDP_PROFILING=1` vs `NETUDP_PROFILING=0`
- Verify `NETUDP_ZONE` is truly a no-op when disabled (check preprocessor expansion)
- Add CMake option: `cmake -DNETUDP_PROFILING=OFF` for production builds
- Document measured overhead in performance analysis

## Impact

- Affected specs: none
- Affected code: `src/profiling/profiler.h`, `CMakeLists.txt`
- Breaking change: NO
- User benefit: +3-6% PPS in production builds, verified profiling cost baseline
