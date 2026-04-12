#pragma once
/**
 * @file bench_main.h
 * @brief Benchmark framework: registry, result types, timing helpers.
 */

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

/* -----------------------------------------------------------------------
 * Result type
 * --------------------------------------------------------------------- */

struct BenchResult {
    std::string          name;
    std::vector<double>  samples_ns;   /* per-operation latency in nanoseconds */
    double               ops_per_sec = 0.0; /* 0 = not applicable              */
    double               p50_ns  = 0.0;
    double               p95_ns  = 0.0;
    double               p99_ns  = 0.0;
    double               max_ns  = 0.0;
};

/* -----------------------------------------------------------------------
 * Configuration (passed to every benchmark function)
 * --------------------------------------------------------------------- */

struct BenchConfig {
    int warmup_iters  = 3;  /* warm-up iterations before measuring */
    int measure_iters = 10; /* timing samples to collect            */
};

/* -----------------------------------------------------------------------
 * Registry
 * --------------------------------------------------------------------- */

class BenchRegistry {
public:
    using BenchFn = std::function<BenchResult(const BenchConfig&)>;

    void add(const std::string& name, BenchFn fn);

    /** Run all benchmarks whose name contains `filter` (nullptr = run all).
     *  Writes JSON to `json_path` when non-null.  Returns 0 on success. */
    int run(const char* filter, const BenchConfig& cfg, const char* json_path);

    static BenchRegistry& global();

private:
    struct Entry { std::string name; BenchFn fn; };
    std::vector<Entry> entries_;
};

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

/** Compute the p-th percentile of `s` (0–100).  Sorts in-place. */
inline double bench_percentile(std::vector<double>& s, double p) {
    if (s.empty()) return 0.0;
    std::sort(s.begin(), s.end());
    const size_t last = s.size() - 1U;
    const size_t idx  = static_cast<size_t>(p / 100.0 * static_cast<double>(last));
    return s[idx];
}

/** Time a callable once; return elapsed nanoseconds as double. */
template <typename Fn>
double bench_time_ns(Fn&& fn) {
    using Clock = std::chrono::high_resolution_clock;
    auto t0 = Clock::now();
    fn();
    auto t1 = Clock::now();
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
}
