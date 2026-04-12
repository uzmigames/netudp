## 1. Implementation

- [ ] 1.1 Add `SO_REUSEPORT` option to `socket_create()` on Linux
- [ ] 1.2 Design worker thread pool: one socket per thread, thread-local connection ownership
- [ ] 1.3 Implement 5-tuple hash for deterministic connection-to-thread routing
- [ ] 1.4 Add `netudp_server_config_t::num_threads` field (default 1 for backward compat)
- [ ] 1.5 Implement per-thread event loop with independent recv/send cycles
- [ ] 1.6 Add `netudp_server_set_affinity(thread_id, cpu_id)` API
- [ ] 1.7 Implement NUMA-aware pool allocation on Linux (`numa_alloc_onnode` if available)
- [ ] 1.8 Benchmark 1-thread vs 2-thread vs 4-thread PPS on Linux
- [ ] 1.9 Build and verify: `cmake --build build --config Release`

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
