/**
 * @file bench_simd_compare.cpp
 * @brief Side-by-side microbenchmarks for generic vs SSE4.2 vs AVX2 SIMD ops.
 *
 * Benchmarks: crc32c, memcpy_nt, ack_bits_scan, replay_check.
 * Each is run against g_ops_generic, g_ops_sse42, g_ops_avx2 where available.
 */

#include "bench_main.h"
#include "../src/simd/netudp_simd.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace netudp::simd;

/* Benchmark data sizes */
static constexpr int kDataLen   = 1400; /* typical MTU payload */
static constexpr int kInnerReps = 100000;

/* Aligned scratch buffer for memcpy_nt */
static uint8_t s_src[kDataLen];
static uint8_t s_dst[kDataLen];

static uint64_t s_replay_window[128] = {};

/* -----------------------------------------------------------------------
 * Per-ISA timing helper
 * --------------------------------------------------------------------- */

struct SimdTrial {
    const char*     isa_name;
    const SimdOps*  ops;
};

static SimdTrial s_trials[] = {
    { "generic", &g_ops_generic },
    { "sse42",   &g_ops_sse42   },
    { "avx2",    &g_ops_avx2    },
};
static constexpr int kNumTrials = 3;

/* Run `inner_reps` iterations of fn; return per-op nanoseconds */
template <typename Fn>
static double time_per_op(Fn&& fn, int inner_reps) {
    double ns = bench_time_ns([&]() {
        for (int i = 0; i < inner_reps; ++i) { fn(); }
    });
    return ns / static_cast<double>(inner_reps);
}

/* -----------------------------------------------------------------------
 * CRC32C
 * --------------------------------------------------------------------- */

static BenchResult bench_crc32c_isa(const BenchConfig& cfg, const SimdTrial& trial) {
    BenchResult r;
    r.name = std::string("crc32c_") + trial.isa_name;
    r.samples_ns.reserve(static_cast<size_t>(cfg.measure_iters));

    const SimdOps* ops = trial.ops;
    for (int s = 0; s < cfg.measure_iters; ++s) {
        double ns = time_per_op([&]() {
            (void)ops->crc32c(s_src, kDataLen);
        }, kInnerReps);
        r.samples_ns.push_back(ns);
    }
    r.ops_per_sec = (r.samples_ns[0] > 0.0)
                  ? 1e9 / r.samples_ns[0]
                  : 0.0;
    return r;
}

/* -----------------------------------------------------------------------
 * Memcpy NT
 * --------------------------------------------------------------------- */

static BenchResult bench_memcpy_isa(const BenchConfig& cfg, const SimdTrial& trial) {
    BenchResult r;
    r.name = std::string("memcpy_nt_") + trial.isa_name;
    r.samples_ns.reserve(static_cast<size_t>(cfg.measure_iters));

    const SimdOps* ops = trial.ops;
    for (int s = 0; s < cfg.measure_iters; ++s) {
        double ns = time_per_op([&]() {
            ops->memcpy_nt(s_dst, s_src, static_cast<size_t>(kDataLen));
        }, kInnerReps);
        r.samples_ns.push_back(ns);
    }
    r.ops_per_sec = (r.samples_ns[0] > 0.0)
                  ? 1e9 / r.samples_ns[0]
                  : 0.0;
    return r;
}

/* -----------------------------------------------------------------------
 * Ack-bits scan
 * --------------------------------------------------------------------- */

static BenchResult bench_ack_scan_isa(const BenchConfig& cfg, const SimdTrial& trial) {
    BenchResult r;
    r.name = std::string("ack_scan_") + trial.isa_name;
    r.samples_ns.reserve(static_cast<size_t>(cfg.measure_iters));

    int indices[32];
    const SimdOps* ops = trial.ops;
    for (int s = 0; s < cfg.measure_iters; ++s) {
        double ns = time_per_op([&]() {
            (void)ops->ack_bits_scan(0xDEADBEEFU, indices);
        }, kInnerReps);
        r.samples_ns.push_back(ns);
    }
    r.ops_per_sec = (r.samples_ns[0] > 0.0)
                  ? 1e9 / r.samples_ns[0]
                  : 0.0;
    return r;
}

/* -----------------------------------------------------------------------
 * Replay check
 * --------------------------------------------------------------------- */

static BenchResult bench_replay_isa(const BenchConfig& cfg, const SimdTrial& trial) {
    BenchResult r;
    r.name = std::string("replay_check_") + trial.isa_name;
    r.samples_ns.reserve(static_cast<size_t>(cfg.measure_iters));

    /* Pre-populate window so the check actually has to scan */
    for (int i = 0; i < 128; ++i) { s_replay_window[i] = static_cast<uint64_t>(i * 2); }

    const SimdOps* ops = trial.ops;
    uint64_t seq = 9999;
    for (int s = 0; s < cfg.measure_iters; ++s) {
        double ns = time_per_op([&]() {
            (void)ops->replay_check(s_replay_window, seq, 128);
            seq += 1;
        }, kInnerReps / 100); /* shorter inner loop — seq must advance */
        r.samples_ns.push_back(ns);
    }
    r.ops_per_sec = (r.samples_ns[0] > 0.0)
                  ? 1e9 / r.samples_ns[0]
                  : 0.0;
    return r;
}

/* -----------------------------------------------------------------------
 * Speedup report helper
 * --------------------------------------------------------------------- */

static void print_speedup(const char* op, const BenchResult& base,
                           const BenchResult& accel, const char* accel_name) {
    if (base.p50_ns > 0.0 && accel.p50_ns > 0.0) {
        double speedup = base.p50_ns / accel.p50_ns;
        std::printf("          %s speedup vs generic: %.2fx (%s)\n",
                    op, speedup, accel_name);
    }
}

/* -----------------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------------- */

static BenchResult run_simd_suite(const BenchConfig& cfg) {
    /* Initialise scratch data */
    for (int i = 0; i < kDataLen; ++i) { s_src[i] = static_cast<uint8_t>(i); }

    /* We aggregate all SIMD results into a single "representative" BenchResult
     * for the registry, but print per-ISA details inline. */
    BenchResult aggregate;
    aggregate.name = "simd_suite";

    for (int t = 0; t < kNumTrials; ++t) {
        const SimdTrial& tr = s_trials[t];

        /* Run all four operations for this ISA */
        BenchResult crc  = bench_crc32c_isa(cfg, tr);
        BenchResult mcpy = bench_memcpy_isa(cfg, tr);
        BenchResult ack  = bench_ack_scan_isa(cfg, tr);
        BenchResult rep  = bench_replay_isa(cfg, tr);

        /* Compute percentiles inline for reporting */
        double crc_p50  = bench_percentile(crc.samples_ns,  50.0);
        double mcpy_p50 = bench_percentile(mcpy.samples_ns, 50.0);
        double ack_p50  = bench_percentile(ack.samples_ns,  50.0);
        double rep_p50  = bench_percentile(rep.samples_ns,  50.0);

        std::printf("          [%s]  crc32c=%.1fns  memcpy_nt=%.1fns"
                    "  ack_scan=%.1fns  replay=%.1fns\n",
                    tr.isa_name, crc_p50, mcpy_p50, ack_p50, rep_p50);
        std::fflush(stdout);

        /* Use generic as baseline for speedup reporting */
        if (t == 0) {
            /* baseline — store in aggregate.samples_ns[0..3] as placeholders */
            aggregate.samples_ns.push_back(crc_p50);
            aggregate.samples_ns.push_back(mcpy_p50);
            aggregate.samples_ns.push_back(ack_p50);
            aggregate.samples_ns.push_back(rep_p50);
        } else if (t == 1) {
            /* SSE4.2 vs generic */
            if (aggregate.samples_ns.size() >= 4U) {
                double crc_base = aggregate.samples_ns[0];
                if (crc_base > 0.0 && crc_p50 > 0.0) {
                    std::printf("          SSE4.2 crc32c speedup: %.2fx\n",
                                crc_base / crc_p50);
                }
            }
        } else if (t == 2) {
            /* AVX2 vs generic */
            if (aggregate.samples_ns.size() >= 4U) {
                double crc_base = aggregate.samples_ns[0];
                if (crc_base > 0.0 && crc_p50 > 0.0) {
                    std::printf("          AVX2  crc32c speedup: %.2fx\n",
                                crc_base / crc_p50);
                }
            }
        }
    }

    /* ops_per_sec = generic crc32c throughput as representative figure */
    if (!aggregate.samples_ns.empty() && aggregate.samples_ns[0] > 0.0) {
        aggregate.ops_per_sec = 1e9 / aggregate.samples_ns[0];
    }

    return aggregate;
}

void register_simd_bench(BenchRegistry& reg) {
    reg.add("simd_compare", [](const BenchConfig& cfg) -> BenchResult {
        return run_simd_suite(cfg);
    });
}
