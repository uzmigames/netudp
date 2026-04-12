#ifndef NETUDP_PROFILING_PROFILER_H
#define NETUDP_PROFILING_PROFILER_H

/**
 * @file profiler.h
 * @brief Internal profiling API for netudp.
 *
 * Two modes, selected at build time:
 *
 * 1. Tracy mode (NETUDP_ENABLE_TRACY=ON)
 *    NETUDP_ZONE(name)       → ZoneScopedN(name)
 *    NETUDP_COUNTER(name, v) → TracyPlot(name, v)
 *    The built-in accumulator is inactive.
 *
 * 2. Built-in mode (default)
 *    Lock-free per-zone accumulators (atomics).  Each named zone is
 *    registered once (static local index) and looked up O(1) via the index.
 *    netudp_profiling_get_zones() returns a snapshot.
 *
 * Usage:
 *   void my_function() {
 *       NETUDP_ZONE("my_function");
 *       ...
 *   }
 *
 * Thread safety: zone registration is done once per TU (static local),
 * accumulation uses relaxed atomics — safe for concurrent calls from the
 * same or different threads.
 */

#include <netudp/netudp_profiling.h>
#include <atomic>
#include <cstdint>
#include <climits>

/* ======================================================================
 * Tracy mode
 * ====================================================================== */

#ifdef NETUDP_ENABLE_TRACY
#include <tracy/Tracy.hpp>

#define NETUDP_ZONE(name)        ZoneScopedN(name)
#define NETUDP_COUNTER(name, v)  TracyPlot(name, static_cast<int64_t>(v))

namespace netudp {
    /* Stubs: Tracy handles everything; built-in state unused. */
    inline void  profiler_enable(int)     {}
    inline int   profiler_is_enabled()    { return 1; }
    inline int   profiler_get_zones(netudp_profile_zone_t*, int) { return 0; }
    inline void  profiler_reset()         {}
}

#else /* built-in profiler */

/* ======================================================================
 * Built-in mode — lock-free zone accumulator
 * ====================================================================== */

namespace netudp {

static constexpr int PROFILER_MAX_ZONES = NETUDP_MAX_PROFILE_ZONES;

/** Per-zone accumulated statistics (all atomics, 64-byte aligned). */
struct alignas(64) ZoneStats {
    std::atomic<uint64_t> call_count{0};
    std::atomic<uint64_t> total_ns{0};
    std::atomic<uint64_t> min_ns{UINT64_MAX};
    std::atomic<uint64_t> max_ns{0};
    std::atomic<uint64_t> last_ns{0};
    const char*           name{nullptr};
    uint8_t               _pad[64 - 5 * sizeof(std::atomic<uint64_t>) - sizeof(const char*)];
};

/** Global zone registry — zero-initialised. */
extern ZoneStats          g_zones[PROFILER_MAX_ZONES];
extern std::atomic<int>   g_zone_count;
extern std::atomic<bool>  g_profiling_enabled;

/**
 * Register a zone by name, returning its index.
 * Safe to call from static initialisation (function-local statics).
 * Returns -1 when the registry is full.
 */
int register_zone(const char* name);

/**
 * High-resolution timestamp in nanoseconds.
 * Uses QueryPerformanceCounter on Windows, CLOCK_MONOTONIC_RAW on Linux/macOS.
 */
uint64_t profiler_now_ns();

/** RAII zone: records start time on construction, updates stats on destruction. */
struct ProfileZone {
    int      idx;
    uint64_t start;

    explicit ProfileZone(int zone_idx) noexcept
        : idx(zone_idx)
        , start(0) {
        if (g_profiling_enabled.load(std::memory_order_relaxed) && idx >= 0) {
            start = profiler_now_ns();
        }
    }

    ~ProfileZone() noexcept {
        if (!g_profiling_enabled.load(std::memory_order_relaxed) || idx < 0) {
            return;
        }
        uint64_t elapsed = profiler_now_ns() - start;
        ZoneStats& s = g_zones[idx];

        s.call_count.fetch_add(1, std::memory_order_relaxed);
        s.total_ns.fetch_add(elapsed, std::memory_order_relaxed);
        s.last_ns.store(elapsed, std::memory_order_relaxed);

        /* CAS loop for min */
        uint64_t cur = s.min_ns.load(std::memory_order_relaxed);
        while (elapsed < cur &&
               !s.min_ns.compare_exchange_weak(cur, elapsed,
                   std::memory_order_relaxed, std::memory_order_relaxed)) {}

        /* CAS loop for max */
        cur = s.max_ns.load(std::memory_order_relaxed);
        while (elapsed > cur &&
               !s.max_ns.compare_exchange_weak(cur, elapsed,
                   std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }

    /* Non-copyable / non-movable */
    ProfileZone(const ProfileZone&)            = delete;
    ProfileZone& operator=(const ProfileZone&) = delete;
};

void profiler_enable(int enabled);
int  profiler_is_enabled();
int  profiler_get_zones(netudp_profile_zone_t* out, int max_zones);
void profiler_reset();

} // namespace netudp

/* ======================================================================
 * Internal macros
 * ====================================================================== */

/**
 * NETUDP_ZONE(name)
 * Declares a static zone index (registered once per call site) and an
 * RAII ProfileZone guard for the current scope.
 *
 * The static registration is thread-safe in C++11 (magic statics), and
 * because register_zone() is idempotent once the index is set, concurrent
 * first-entry is also safe.
 *
 * NZ_CAT2/NZ_CAT force __LINE__ to expand before token-paste.
 * This is required by MSVC where ##__LINE__ does not expand __LINE__.
 */
#define NZ_CAT2(a, b) a##b
#define NZ_CAT(a, b)  NZ_CAT2(a, b)
#define NETUDP_ZONE(name)                                                        \
    static const int NZ_CAT(_nz_idx_, __LINE__) = netudp::register_zone(name);  \
    netudp::ProfileZone NZ_CAT(_nz_guard_, __LINE__)(NZ_CAT(_nz_idx_, __LINE__))

/** Emit a named counter value (no-op in built-in mode — use zone stats). */
#define NETUDP_COUNTER(name, v) (void)(v)

#endif /* NETUDP_ENABLE_TRACY */

#endif /* NETUDP_PROFILING_PROFILER_H */
