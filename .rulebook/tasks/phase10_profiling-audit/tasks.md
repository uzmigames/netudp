## 1. Implementation

- [ ] 1.1 Benchmark PPS with `NETUDP_PROFILING=1` (current default)
- [ ] 1.2 Verify `NETUDP_ZONE` macro expands to no-op when profiling is disabled
- [ ] 1.3 Add CMake option `NETUDP_PROFILING` (default ON for dev, OFF for release)
- [ ] 1.4 Benchmark PPS with `NETUDP_PROFILING=0` and measure delta
- [ ] 1.5 If overhead > 3%: optimize zone accumulator (batch rdtsc, reduce atomics)
- [ ] 1.6 Build and verify: `cmake --build build --config Release`

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
