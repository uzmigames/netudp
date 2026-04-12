#include "netudp_simd.h"
#include "../core/platform.h"

#if defined(NETUDP_ARCH_X64) || defined(NETUDP_ARCH_X86)
#if defined(NETUDP_COMPILER_MSVC)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace netudp {
namespace simd {

const SimdOps* g_simd = nullptr;

#if defined(NETUDP_ARCH_X64) || defined(NETUDP_ARCH_X86)

static void cpuid(int info[4], int function_id) {
#if defined(NETUDP_COMPILER_MSVC)
    __cpuid(info, function_id);
#else
    __cpuid(function_id, info[0], info[1], info[2], info[3]);
#endif
}

static void cpuidex(int info[4], int function_id, int subfunction_id) {
#if defined(NETUDP_COMPILER_MSVC)
    __cpuidex(info, function_id, subfunction_id);
#else
    __cpuid_count(function_id, subfunction_id, info[0], info[1], info[2], info[3]);
#endif
}

static netudp_simd_level_t detect_x86() {
    int info[4] = {};

    cpuid(info, 0);
    int max_function = info[0];
    if (max_function < 1) {
        return NETUDP_SIMD_GENERIC;
    }

    cpuid(info, 1);
    bool has_sse42 = (info[2] & (1 << 20)) != 0;   /* ECX bit 20: SSE4.2 */
    bool has_popcnt = (info[2] & (1 << 23)) != 0;   /* ECX bit 23: POPCNT */

    bool has_avx2 = false;
    if (max_function >= 7) {
        cpuidex(info, 7, 0);
        has_avx2 = (info[1] & (1 << 5)) != 0;       /* EBX bit 5: AVX2 */
    }

    if (has_avx2) {
        return NETUDP_SIMD_AVX2;
    }
    if (has_sse42 && has_popcnt) {
        return NETUDP_SIMD_SSE42;
    }
    return NETUDP_SIMD_GENERIC;
}

#endif /* x86 */

netudp_simd_level_t detect_and_set() {
    netudp_simd_level_t level = NETUDP_SIMD_GENERIC;

#if defined(NETUDP_ARCH_X64) || defined(NETUDP_ARCH_X86)
    level = detect_x86();

    switch (level) {
        case NETUDP_SIMD_AVX2:
            g_simd = &g_ops_avx2;
            break;
        case NETUDP_SIMD_SSE42:
            g_simd = &g_ops_sse42;
            break;
        default:
            g_simd = &g_ops_generic;
            break;
    }
#elif defined(NETUDP_ARCH_ARM64)
    level = NETUDP_SIMD_NEON;
    g_simd = &g_ops_neon;
#else
    g_simd = &g_ops_generic;
#endif

    return level;
}

} // namespace simd
} // namespace netudp
