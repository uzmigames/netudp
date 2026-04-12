#ifndef NETUDP_PROFILING_H
#define NETUDP_PROFILING_H

/**
 * @file netudp_profiling.h
 * @brief Public logging and profiling API for netudp.
 *
 * Logging
 * -------
 * Set a global log callback with netudp_set_log_callback().  The callback
 * receives the level, source file, line number, formatted message, and the
 * userdata pointer you supplied.  Only messages at or above the current
 * minimum level are forwarded.
 *
 * Per-server/client log_callback fields in the config structs override the
 * global callback for that instance.
 *
 * Profiling
 * ---------
 * When enabled, the library accumulates timing statistics for every named
 * zone it passes through (packet encrypt/decrypt, channel flush, server
 * update, etc.).  Call netudp_profiling_get_zones() to read a snapshot.
 *
 * Tracy integration
 * -----------------
 * Build with -DNETUDP_ENABLE_TRACY=ON to forward all zone markers and
 * counters to Tracy (https://github.com/wolfpld/tracy).  The built-in
 * accumulator is bypassed when Tracy is active.
 */

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * Log levels
 * ====================================================================== */

#define NETUDP_LOG_TRACE  0
#define NETUDP_LOG_DEBUG  1
#define NETUDP_LOG_INFO   2
#define NETUDP_LOG_WARN   3
#define NETUDP_LOG_ERROR  4

/**
 * Log callback type.
 * @param level    One of NETUDP_LOG_*.
 * @param file     Source file (compile-time constant, do not free).
 * @param line     Source line number.
 * @param msg      Null-terminated formatted message (valid only during call).
 * @param userdata Opaque pointer supplied to netudp_set_log_callback().
 */
typedef void (*netudp_log_fn)(int level, const char* file, int line,
                               const char* msg, void* userdata);

/**
 * Set the global log callback.
 * Thread-safe: stored atomically.  Pass NULL to disable logging.
 */
void netudp_set_log_callback(netudp_log_fn fn, void* userdata);

/**
 * Set the minimum log level.  Messages below this level are dropped before
 * the callback is invoked.  Default: NETUDP_LOG_INFO.
 */
void netudp_set_log_level(int min_level);

/* ======================================================================
 * Profiling
 * ====================================================================== */

/** Maximum number of distinct zones the built-in profiler can track. */
#define NETUDP_MAX_PROFILE_ZONES 64

/**
 * Snapshot of accumulated statistics for one named zone.
 * All times are in nanoseconds.
 */
typedef struct netudp_profile_zone {
    const char* name;        /**< Zone name (compile-time constant). */
    uint64_t    call_count;  /**< Number of times this zone was entered. */
    uint64_t    total_ns;    /**< Sum of all elapsed times. */
    uint64_t    min_ns;      /**< Minimum single elapsed time. */
    uint64_t    max_ns;      /**< Maximum single elapsed time. */
    uint64_t    last_ns;     /**< Elapsed time of the most recent call. */
} netudp_profile_zone_t;

/**
 * Enable (1) or disable (0) the built-in profiler.
 * Has no effect when the library was built with Tracy support.
 * Default: disabled (0).
 */
void netudp_profiling_enable(int enabled);

/** Returns 1 if the built-in profiler is currently enabled, else 0. */
int netudp_profiling_is_enabled(void);

/**
 * Copy the current zone statistics into @p out.
 * @param out       Caller-supplied array of at least @p max_zones entries.
 * @param max_zones Maximum number of entries to write.
 * @return          Number of zones written (<= max_zones).
 */
int netudp_profiling_get_zones(netudp_profile_zone_t* out, int max_zones);

/** Reset all accumulated zone statistics to zero. */
void netudp_profiling_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* NETUDP_PROFILING_H */
