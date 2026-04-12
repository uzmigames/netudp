/**
 * @file profiler.cpp
 * @brief Built-in profiler implementation.
 *
 * Compiled only when NETUDP_ENABLE_TRACY is NOT set.  When Tracy is enabled,
 * this translation unit is still compiled but all functions are stubs.
 */

#include "profiler.h"
#include <cstring>
#include <climits>

#ifdef NETUDP_ENABLE_TRACY
/* Tracy mode — stubs only (Tracy owns all profiling). */
#else

/* ======================================================================
 * Platform-specific high-resolution timer
 * ====================================================================== */

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace netudp {

static LARGE_INTEGER g_qpc_freq = {};
static bool          g_qpc_init = false;

static void ensure_qpc_init() {
    if (!g_qpc_init) {
        QueryPerformanceFrequency(&g_qpc_freq);
        g_qpc_init = true;
    }
}

uint64_t profiler_now_ns() {
    ensure_qpc_init();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    /* Multiply first, divide last to preserve precision. */
    return static_cast<uint64_t>(counter.QuadPart) * 1000000000ULL /
           static_cast<uint64_t>(g_qpc_freq.QuadPart);
}

} // namespace netudp

#else /* POSIX */

#include <time.h>

namespace netudp {

uint64_t profiler_now_ns() {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC_RAW)
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

} // namespace netudp

#endif /* platform */

/* ======================================================================
 * Global state
 * ====================================================================== */

namespace netudp {

ZoneStats         g_zones[PROFILER_MAX_ZONES];
std::atomic<int>  g_zone_count{0};
std::atomic<bool> g_profiling_enabled{false};

/* ======================================================================
 * Zone registration
 * ====================================================================== */

int register_zone(const char* name) {
    /* Claim the next slot (one atomic increment, then write name once). */
    int idx = g_zone_count.fetch_add(1, std::memory_order_relaxed);
    if (idx >= PROFILER_MAX_ZONES) {
        g_zone_count.store(PROFILER_MAX_ZONES, std::memory_order_relaxed);
        return -1;
    }
    g_zones[idx].name       = name;
    g_zones[idx].call_count.store(0,         std::memory_order_relaxed);
    g_zones[idx].total_ns.store(0,           std::memory_order_relaxed);
    g_zones[idx].min_ns.store(UINT64_MAX,    std::memory_order_relaxed);
    g_zones[idx].max_ns.store(0,             std::memory_order_relaxed);
    g_zones[idx].last_ns.store(0,            std::memory_order_relaxed);
    return idx;
}

/* ======================================================================
 * Public API helpers (called from api.cpp / extern "C")
 * ====================================================================== */

void profiler_enable(int enabled) {
    g_profiling_enabled.store(enabled != 0, std::memory_order_release);
}

int profiler_is_enabled() {
    return g_profiling_enabled.load(std::memory_order_relaxed) ? 1 : 0;
}

int profiler_get_zones(netudp_profile_zone_t* out, int max_zones) {
    if (out == nullptr || max_zones <= 0) {
        return 0;
    }
    int count = g_zone_count.load(std::memory_order_relaxed);
    if (count > PROFILER_MAX_ZONES) {
        count = PROFILER_MAX_ZONES;
    }
    int n = (count < max_zones) ? count : max_zones;
    for (int i = 0; i < n; ++i) {
        ZoneStats& s = g_zones[i];
        out[i].name       = s.name;
        out[i].call_count = s.call_count.load(std::memory_order_relaxed);
        out[i].total_ns   = s.total_ns.load(std::memory_order_relaxed);
        uint64_t mn       = s.min_ns.load(std::memory_order_relaxed);
        out[i].min_ns     = (mn == UINT64_MAX) ? 0 : mn;
        out[i].max_ns     = s.max_ns.load(std::memory_order_relaxed);
        out[i].last_ns    = s.last_ns.load(std::memory_order_relaxed);
    }
    return n;
}

void profiler_reset() {
    int count = g_zone_count.load(std::memory_order_relaxed);
    if (count > PROFILER_MAX_ZONES) {
        count = PROFILER_MAX_ZONES;
    }
    for (int i = 0; i < count; ++i) {
        g_zones[i].call_count.store(0,       std::memory_order_relaxed);
        g_zones[i].total_ns.store(0,         std::memory_order_relaxed);
        g_zones[i].min_ns.store(UINT64_MAX,  std::memory_order_relaxed);
        g_zones[i].max_ns.store(0,           std::memory_order_relaxed);
        g_zones[i].last_ns.store(0,          std::memory_order_relaxed);
    }
}

} // namespace netudp

#endif /* !NETUDP_ENABLE_TRACY */
