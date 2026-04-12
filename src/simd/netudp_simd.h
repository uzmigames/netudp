#ifndef NETUDP_SIMD_H
#define NETUDP_SIMD_H

/**
 * @file netudp_simd.h
 * @brief SIMD dispatch table and detection.
 *
 * Function pointers resolved once at netudp_init(). All hot-path operations
 * go through g_simd-> for zero-overhead dispatch to the best available ISA.
 */

#include <netudp/netudp_types.h>
#include "../core/platform.h"
#include <cstddef>
#include <cstdint>

namespace netudp {
namespace simd {

struct SimdOps {
    /* Integrity */
    uint32_t (*crc32c)(const uint8_t* data, int len);

    /* Memory */
    void (*memcpy_nt)(void* dst, const void* src, size_t len);
    void (*memset_zero)(void* dst, size_t len);

    /* Ack processing */
    int (*ack_bits_scan)(uint32_t bits, int* indices);
    int (*popcount32)(uint32_t v);

    /* Replay */
    int (*replay_check)(const uint64_t* window, uint64_t seq, int size);

    /* Fragment */
    int (*fragment_bitmask_complete)(const uint8_t* mask, int total);
    int (*fragment_next_missing)(const uint8_t* mask, int total);

    /* Stats */
    void (*accumulate_u64)(uint64_t* dst, const uint64_t* src, int count);

    /* Address */
    int (*addr_equal)(const void* a, const void* b, int len);
};

/* Set once at netudp_init(), never changes after. */
extern const SimdOps* g_simd;

/* Detect CPU features and set g_simd to the best available implementation. */
netudp_simd_level_t detect_and_set();

/* Per-ISA dispatch tables (defined in simd_*.cpp files) */
extern const SimdOps g_ops_generic;
extern const SimdOps g_ops_sse42;
extern const SimdOps g_ops_avx2;
extern const SimdOps g_ops_neon;

} // namespace simd
} // namespace netudp

#endif /* NETUDP_SIMD_H */
