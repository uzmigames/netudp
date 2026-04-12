#ifndef NETUDP_CORE_LOG_H
#define NETUDP_CORE_LOG_H

/**
 * @file log.h
 * @brief Internal logging macros for netudp.
 *
 * All public-facing log types are declared in netudp_profiling.h.
 * This header wires in the global log state and provides the NETUDP_LOG*
 * macros used throughout the implementation.
 *
 * Hot-path note: the level check is a single integer comparison against a
 * non-atomic int (written under a mutex, read without one — benign race,
 * level changes are advisory).  The callback pointer is read atomically so
 * that netudp_set_log_callback(NULL, ...) is always safe.
 */

#include <netudp/netudp_profiling.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace netudp {

/** Global log state — zero-initialised at program start. */
struct LogState {
    std::atomic<netudp_log_fn> callback{nullptr};
    std::atomic<void*>         userdata{nullptr};
    int                        min_level{NETUDP_LOG_INFO};
};

extern LogState g_log;

/**
 * Format and dispatch a log message.
 * Called only when the level check has already passed (see macros below).
 */
inline void log_write(int level, const char* file, int line,
                      const char* fmt, ...) {
    netudp_log_fn fn = g_log.callback.load(std::memory_order_acquire);
    if (fn == nullptr) {
        return;
    }
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    void* ud = g_log.userdata.load(std::memory_order_relaxed);
    fn(level, file, line, buf, ud);
}

} // namespace netudp

/* ======================================================================
 * Logging macros — zero-overhead when callback is null or level filtered
 *
 * Note: NETUDP_LOG_TRACE .. NETUDP_LOG_ERROR are integer constants defined
 * in netudp_profiling.h (0-4).  The function-like logging macros use shorter
 * names to avoid redefinition: NLOG_TRACE, NLOG_DEBUG, NLOG_INFO,
 * NLOG_WARN, NLOG_ERROR.
 * ====================================================================== */

#define NLOG(level, ...)                                                    \
    do {                                                                    \
        if ((level) >= netudp::g_log.min_level &&                          \
            netudp::g_log.callback.load(std::memory_order_relaxed) != nullptr) \
            netudp::log_write((level), __FILE__, __LINE__, __VA_ARGS__);   \
    } while (0)

#define NLOG_TRACE(...) NLOG(NETUDP_LOG_TRACE, __VA_ARGS__)
#define NLOG_DEBUG(...) NLOG(NETUDP_LOG_DEBUG, __VA_ARGS__)
#define NLOG_INFO(...)  NLOG(NETUDP_LOG_INFO,  __VA_ARGS__)
#define NLOG_WARN(...)  NLOG(NETUDP_LOG_WARN,  __VA_ARGS__)
#define NLOG_ERROR(...) NLOG(NETUDP_LOG_ERROR, __VA_ARGS__)

#endif /* NETUDP_CORE_LOG_H */
