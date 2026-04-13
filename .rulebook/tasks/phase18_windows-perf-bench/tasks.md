## 1. Implementation

- [ ] 1.1 Create `bench_socket_backends.cpp`: raw loopback PPS test per backend (sendto, WSASendTo, RIO)
- [ ] 1.2 Create `bench_coalescing.cpp`: send 1/5/10/20 msgs per tick, measure packets/s and msgs/s
- [ ] 1.3 Register new benchmarks in `netudp_bench` runner
- [ ] 1.4 Run full benchmark suite: sendto vs WSASendTo vs RIO on same hardware
- [ ] 1.5 Measure WFP impact: benchmark with BFE running vs stopped (document delta)
- [ ] 1.6 Update `docs/analysis/performance/01-current-state.md` with measured backend comparison
- [ ] 1.7 Update `README.md` performance section with Windows backend table
- [ ] 1.8 Build and verify: `cmake --build build --config Release`

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
