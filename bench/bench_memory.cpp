/**
 * @file bench_memory.cpp
 * @brief Working-set (RSS) measurement with many simultaneous server slots.
 *
 * Creates a server configured for `kMaxClients` connections, measures the
 * delta in process working set before and after.  Target: ≤ 100 MB total.
 *
 * Per-connection overhead = total_delta / kMaxClients.
 */

#include "bench_main.h"
#include <netudp/netudp.h>

#include <cstdio>
#include <cstring>

/* Platform RSS helpers */
#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#   include <psapi.h>
static size_t rss_bytes() {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0U;
}
#elif defined(__APPLE__)
#   include <mach/mach.h>
static size_t rss_bytes() {
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<size_t>(info.resident_size);
    }
    return 0U;
}
#else /* Linux */
#   include <sys/resource.h>
static size_t rss_bytes() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    /* ru_maxrss is in kilobytes on Linux */
    return static_cast<size_t>(ru.ru_maxrss) * 1024U;
}
#endif

static constexpr int      kMaxClients       = 1024;
static constexpr uint64_t kMemProtocolId    = 0xBEEF00040000004ULL;
static constexpr uint16_t kMemPort          = 29400U;
static constexpr double   kMiB              = 1.0 / (1024.0 * 1024.0);

static const uint8_t kMemKey[32] = {
    0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0,
    0xC0, 0xD0, 0xE0, 0xF0, 0x01, 0x11, 0x21, 0x31,
    0x41, 0x51, 0x61, 0x71, 0x81, 0x91, 0xA1, 0xB1,
    0xC1, 0xD1, 0xE1, 0xF1, 0x02, 0x12, 0x22, 0x32,
};

static BenchResult run_memory_bench(const BenchConfig& /*cfg*/) {
    BenchResult r;
    r.name = "memory_1024slots";

    size_t rss_before = rss_bytes();

    char srv_addr[64];
    std::snprintf(srv_addr, sizeof(srv_addr), "127.0.0.1:%u",
                  static_cast<unsigned>(kMemPort));

    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id = kMemProtocolId;
    std::memcpy(srv_cfg.private_key, kMemKey, 32);

    netudp_server_t* server = netudp_server_create(srv_addr, &srv_cfg, 8000.0);
    if (server == nullptr) {
        std::fprintf(stderr, "[memory] server_create failed\n");
        return r;
    }
    netudp_server_start(server, kMaxClients);

    size_t rss_after  = rss_bytes();
    size_t rss_delta  = (rss_after > rss_before) ? (rss_after - rss_before) : 0U;

    double delta_mib  = static_cast<double>(rss_delta)  * kMiB;
    double total_mib  = static_cast<double>(rss_after)  * kMiB;
    double per_slot   = (kMaxClients > 0)
                      ? (static_cast<double>(rss_delta) / kMaxClients)
                      : 0.0;

    std::printf("          rss_before    = %.2f MiB\n",
                static_cast<double>(rss_before) * kMiB);
    std::printf("          rss_after     = %.2f MiB\n", total_mib);
    std::printf("          server_delta  = %.2f MiB  (%.1f KB/slot)\n",
                delta_mib, per_slot / 1024.0);
    std::printf("          target        = <= 100 MiB delta  %s\n",
                (delta_mib <= 100.0) ? "[PASS]" : "[WARN: exceeds target]");
    std::fflush(stdout);

    /* Report delta as a single sample (in ns column — repurposed as bytes) */
    r.samples_ns.push_back(delta_mib);   /* MiB stored in samples for p50 output */
    r.ops_per_sec = per_slot;            /* bytes per slot */

    netudp_server_stop(server);
    netudp_server_destroy(server);

    return r;
}

void register_memory_bench(BenchRegistry& reg) {
    reg.add("memory_1024slots", [](const BenchConfig& cfg) -> BenchResult {
        return run_memory_bench(cfg);
    });
}
