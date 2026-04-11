# Spec 14 — Benchmark Suite

## Requirements

### REQ-14.1: Benchmark Categories & Targets

| Benchmark | What | Target | File |
|---|---|---|---|
| `bench_pps` | Packets per second (64B, encrypted, reliable) | ≥ 2M PPS | `bench/bench_pps.cpp` |
| `bench_latency` | End-to-end loopback latency histogram | p99 ≤ 5µs | `bench/bench_latency.cpp` |
| `bench_crc32c` | CRC32C throughput per SIMD level | ≥ 8 GB/s SSE4.2 | `bench/bench_crc32c.cpp` |
| `bench_aead` | ChaCha20-Poly1305 encrypt+decrypt | ≥ 2 GB/s AVX2 | `bench/bench_aead.cpp` |
| `bench_pool` | Pool acquire/release cycle | ≤ 10 ns/op | `bench/bench_pool.cpp` |
| `bench_ack` | Ack bitmask scan + reliability update | ≤ 50 ns/pkt | `bench/bench_ack.cpp` |
| `bench_fragment` | Fragment reassembly (10 fragments) | ≤ 1µs | `bench/bench_fragment.cpp` |
| `bench_nagle` | Pack N messages into 1 packet | ≤ 100 ns/msg | `bench/bench_nagle.cpp` |
| `bench_pipeline` | Full send→recv (loopback, encrypted, reliable) | ≤ 15µs | `bench/bench_pipeline.cpp` |
| `bench_scalability` | PPS vs connection count (1-1000) | Linear decay | `bench/bench_scalability.cpp` |
| `bench_simd` | Side-by-side: generic vs SSE vs AVX vs NEON | ≥ 20% gain | `bench/bench_simd.cpp` |
| `bench_memory` | Memory usage at N connections | Report only | `bench/bench_memory.cpp` |

### REQ-14.2: Framework

```cpp
// bench/bench_main.cpp
struct BenchResult {
    const char* name;
    double ops_per_sec;
    double ns_per_op;
    double throughput_mbps;  // If applicable
    int    iterations;
};

void bench_run(const char* name, int iterations, std::function<void()> fn);
void bench_report(const BenchResult* results, int count);
```

### REQ-14.3: CI Regression

```yaml
# Runs on every PR
# Compares against baseline from main branch
# Fails if any metric regresses > 5%
# Posts results as PR comment
```

### REQ-14.4: SIMD Comparison Rule

No SIMD implementation SHALL be merged without a benchmark proving ≥ 20% improvement over the scalar fallback on the target hardware class.

## Scenarios

#### Scenario: PPS benchmark
Given server and client in loopback
When 10 million 64-byte packets are sent (encrypted, reliable)
Then measured PPS ≥ 2,000,000 on desktop hardware
And results logged with CPU model, SIMD level

#### Scenario: Regression detection
Given main branch baseline: bench_pps = 2.1M PPS
When PR introduces change that drops to 1.9M PPS (9.5% regression)
Then CI fails with "bench_pps regressed 9.5% (threshold: 5%)"
