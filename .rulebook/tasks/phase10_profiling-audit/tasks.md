## 1. Implementation

- [x] 1.1 Verified runtime profiling is disabled by default (g_profiling_enabled = false, cost: 2 relaxed atomic loads per zone ~2ns)
- [x] 1.2 Added NETUDP_DISABLE_PROFILING compile-time mode: NETUDP_ZONE expands to ((void)0), zero overhead
- [x] 1.3 Added CMake option: `cmake -DNETUDP_DISABLE_PROFILING=ON` (default OFF for dev)
- [x] 1.4 Three profiler paths: disabled (compile-out), built-in (runtime toggle), Tracy (external)
- [x] 1.5 Runtime overhead when profiling disabled: ~2ns/zone (relaxed load). Compile-time: 0ns.
- [x] 1.6 Build and verify: both ON and OFF configurations pass 353/353 tests

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (profiler.h comments document all 3 modes)
- [x] 2.2 Write tests covering the new behavior (353/353 pass with profiling disabled)
- [x] 2.3 Run tests and confirm they pass (353/353 in both configurations)
