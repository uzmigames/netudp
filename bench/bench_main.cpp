/**
 * @file bench_main.cpp
 * @brief Benchmark runner: BenchRegistry implementation + main entry point.
 *
 * Usage:
 *   netudp_bench [--bench <substr>] [--warmup <N>] [--runs <N>] [--json <file>]
 */

#include "bench_main.h"
#include <netudp/netudp.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>

/* Forward declarations — one per bench_*.cpp */
void register_pps_bench(BenchRegistry& reg);
void register_latency_bench(BenchRegistry& reg);
void register_simd_bench(BenchRegistry& reg);
void register_scalability_bench(BenchRegistry& reg);
void register_memory_bench(BenchRegistry& reg);

/* -----------------------------------------------------------------------
 * BenchRegistry implementation
 * --------------------------------------------------------------------- */

BenchRegistry& BenchRegistry::global() {
    static BenchRegistry inst;
    return inst;
}

void BenchRegistry::add(const std::string& name, BenchFn fn) {
    entries_.push_back({name, std::move(fn)});
}

static void finalize(BenchResult& r) {
    r.p50_ns = bench_percentile(r.samples_ns, 50.0);
    r.p95_ns = bench_percentile(r.samples_ns, 95.0);
    r.p99_ns = bench_percentile(r.samples_ns, 99.0);
    if (!r.samples_ns.empty()) {
        r.max_ns = *std::max_element(r.samples_ns.begin(), r.samples_ns.end());
    }
}

static void print_json(const std::vector<BenchResult>& results, const char* path) {
    std::FILE* f = std::fopen(path, "w");
    if (f == nullptr) {
        std::fprintf(stderr, "warning: could not open %s for writing\n", path);
        return;
    }
    std::fprintf(f, "[\n");
    for (size_t i = 0; i < results.size(); ++i) {
        const BenchResult& r = results[i];
        std::fprintf(f, "  {\n");
        std::fprintf(f, "    \"name\": \"%s\",\n",       r.name.c_str());
        std::fprintf(f, "    \"ops_per_sec\": %.0f,\n",  r.ops_per_sec);
        std::fprintf(f, "    \"p50_ns\": %.1f,\n",       r.p50_ns);
        std::fprintf(f, "    \"p95_ns\": %.1f,\n",       r.p95_ns);
        std::fprintf(f, "    \"p99_ns\": %.1f,\n",       r.p99_ns);
        std::fprintf(f, "    \"max_ns\": %.1f\n",        r.max_ns);
        std::fprintf(f, "  }%s\n", (i + 1U < results.size()) ? "," : "");
    }
    std::fprintf(f, "]\n");
    std::fclose(f);
    std::printf("JSON results written to: %s\n", path);
}

int BenchRegistry::run(const char* filter, const BenchConfig& cfg,
                       const char* json_path) {
    std::vector<BenchResult> results;

    for (auto& e : entries_) {
        if (filter != nullptr && e.name.find(filter) == std::string::npos) {
            continue;
        }

        std::printf("[ bench ] %-36s  warmup=%d  runs=%d\n",
                    e.name.c_str(), cfg.warmup_iters, cfg.measure_iters);
        std::fflush(stdout);

        /* Warm-up: run once with reduced iters so caches and connections are warm */
        BenchConfig wup = cfg;
        wup.measure_iters = 1;
        for (int w = 0; w < cfg.warmup_iters; ++w) {
            (void)e.fn(wup);
        }

        /* Measure */
        BenchResult r = e.fn(cfg);
        r.name = e.name;
        finalize(r);

        /* Human-readable output */
        if (r.ops_per_sec >= 1e6) {
            std::printf("          ops/sec  = %.2f M\n", r.ops_per_sec / 1e6);
        } else if (r.ops_per_sec >= 1e3) {
            std::printf("          ops/sec  = %.2f K\n", r.ops_per_sec / 1e3);
        } else if (r.ops_per_sec > 0.0) {
            std::printf("          ops/sec  = %.2f\n",   r.ops_per_sec);
        }
        if (!r.samples_ns.empty()) {
            std::printf("          p50      = %.1f ns\n", r.p50_ns);
            std::printf("          p95      = %.1f ns\n", r.p95_ns);
            std::printf("          p99      = %.1f ns\n", r.p99_ns);
            std::printf("          max      = %.1f ns\n", r.max_ns);
        }
        std::printf("\n");
        std::fflush(stdout);

        results.push_back(std::move(r));
    }

    if (json_path != nullptr && !results.empty()) {
        print_json(results, json_path);
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * Entry point
 * --------------------------------------------------------------------- */

static std::string make_bench_output_path(const char* filter) {
    /* benchmarks/YYYY-MM-DD_HH-MM-SS[_<filter>].json */
    std::time_t t  = std::time(nullptr);
    std::tm*    tm = std::localtime(&t);
    char ts[32]    = {};
    std::strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", tm);

    std::filesystem::path dir = "benchmarks";
    std::filesystem::create_directories(dir);

    std::string name = ts;
    if (filter != nullptr && filter[0] != '\0') {
        name += '_';
        name += filter;
    }
    name += ".json";

    return (dir / name).string();
}

int main(int argc, char** argv) {
    if (netudp_init() != NETUDP_OK) {
        std::fprintf(stderr, "netudp_init() failed\n");
        return 1;
    }

    const char* filter       = nullptr;
    const char* json_out_arg = nullptr;
    BenchConfig cfg;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--bench") == 0 && i + 1 < argc) {
            filter = argv[++i];
        } else if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            cfg.warmup_iters = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--runs") == 0 && i + 1 < argc) {
            cfg.measure_iters = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            json_out_arg = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf(
                "Usage: netudp_bench [OPTIONS]\n"
                "  --bench  <substr>  run only benchmarks whose name contains substr\n"
                "  --warmup <N>       warm-up iterations before measuring (default 3)\n"
                "  --runs   <N>       measurement iterations / samples    (default 10)\n"
                "  --json   <file>    override output path (default: benchmarks/<datetime>.json)\n");
            netudp_term();
            return 0;
        }
    }

    /* Default: auto-generate path in benchmarks/ with datetime */
    std::string auto_path;
    const char* json_out = json_out_arg;
    if (json_out == nullptr) {
        auto_path = make_bench_output_path(filter);
        json_out  = auto_path.c_str();
    }

    BenchRegistry& reg = BenchRegistry::global();
    register_pps_bench(reg);
    register_latency_bench(reg);
    register_simd_bench(reg);
    register_scalability_bench(reg);
    register_memory_bench(reg);

    int rc = reg.run(filter, cfg, json_out);

    netudp_term();
    return rc;
}
