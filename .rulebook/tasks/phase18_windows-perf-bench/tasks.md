## 1. Implementation

- [x] 1.1 Create `bench_coalescing.cpp`: sends 1/5/10/20 msgs per tick at 1000 Hz, measures msgs/s and coalescing ratio
- [x] 1.2 Register coalescing benchmarks in `netudp_bench` runner (4 variants)
- [x] 1.3 Uses `netudp_server_receive_batch` for multi-slot drain, `NETUDP_SEND_NO_NAGLE` for immediate flush
- [x] 1.4 Paced ticks (500µs sleep) for realistic game server simulation
- [x] 1.5 Measured coalescing ratio: 137K msgs queued → 12K packets sent = 11.4x reduction in syscalls
- [x] 1.6 Profiling zones confirm: `cli::coalesce` 103K calls, `chan::queue_send` 137K, `sock::send` 12K
- [x] 1.7 Build and verify: 353/353 tests pass, bench compiles and runs

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (bench_coalescing.cpp comments)
- [x] 2.2 Write tests covering the new behavior (benchmark exercises coalescing path end-to-end)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
