#include "netudp_simd.h"

#if defined(NETUDP_ARCH_ARM64)

#include <arm_neon.h>
#include <cstring>

#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#endif

namespace netudp {
namespace simd {

static uint32_t neon_crc32c(const uint8_t* data, int len) {
#if defined(__ARM_FEATURE_CRC32)
    uint32_t crc = 0xFFFFFFFF;
    int i = 0;

    for (; i + 7 < len; i += 8) {
        uint64_t val = 0;
        std::memcpy(&val, data + i, 8);
        crc = __crc32cd(crc, val);
    }
    for (; i + 3 < len; i += 4) {
        uint32_t val = 0;
        std::memcpy(&val, data + i, 4);
        crc = __crc32cw(crc, val);
    }
    for (; i < len; ++i) {
        crc = __crc32cb(crc, data[i]);
    }
    return crc ^ 0xFFFFFFFF;
#else
    return g_ops_generic.crc32c(data, len);
#endif
}

static void neon_memcpy_nt(void* dst, const void* src, size_t len) {
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    size_t i = 0;

    for (; i + 15 < len; i += 16) {
        uint8x16_t v = vld1q_u8(s + i);
        vst1q_u8(d + i, v);
    }
    for (; i < len; ++i) {
        d[i] = s[i];
    }
}

static void neon_memset_zero(void* dst, size_t len) {
    auto* d = static_cast<uint8_t*>(dst);
    uint8x16_t zero = vdupq_n_u8(0);
    size_t i = 0;

    for (; i + 15 < len; i += 16) {
        vst1q_u8(d + i, zero);
    }
    for (; i < len; ++i) {
        d[i] = 0;
    }
}

static int neon_ack_bits_scan(uint32_t bits, int* indices) {
    int count = 0;
    while (bits != 0) {
        int idx = __builtin_ctz(bits);
        indices[count++] = idx;
        bits &= bits - 1;
    }
    return count;
}

static int neon_popcount32(uint32_t v) {
    uint8x8_t input = vcreate_u8(static_cast<uint64_t>(v));
    uint8x8_t counts = vcnt_u8(input);
    return vaddv_u8(counts);
}

static int neon_replay_check(const uint64_t* window, uint64_t seq, int size) {
    uint64x2_t target = vdupq_n_u64(seq);
    int i = 0;
    for (; i + 1 < size; i += 2) {
        uint64x2_t vals = vld1q_u64(window + i);
        uint64x2_t cmp = vceqq_u64(vals, target);
        if (vgetq_lane_u64(cmp, 0) != 0 || vgetq_lane_u64(cmp, 1) != 0) {
            return 1;
        }
    }
    for (; i < size; ++i) {
        if (window[i] == seq) {
            return 1;
        }
    }
    return 0;
}

static int neon_fragment_bitmask_complete(const uint8_t* mask, int total) {
    return g_ops_generic.fragment_bitmask_complete(mask, total);
}

static int neon_fragment_next_missing(const uint8_t* mask, int total) {
    return g_ops_generic.fragment_next_missing(mask, total);
}

static void neon_accumulate_u64(uint64_t* dst, const uint64_t* src, int count) {
    int i = 0;
    for (; i + 1 < count; i += 2) {
        uint64x2_t a = vld1q_u64(dst + i);
        uint64x2_t b = vld1q_u64(src + i);
        vst1q_u64(dst + i, vaddq_u64(a, b));
    }
    for (; i < count; ++i) {
        dst[i] += src[i];
    }
}

static int neon_addr_equal(const void* a, const void* b, int len) {
    if (len >= 16) {
        uint8x16_t va = vld1q_u8(static_cast<const uint8_t*>(a));
        uint8x16_t vb = vld1q_u8(static_cast<const uint8_t*>(b));
        uint8x16_t cmp = vceqq_u8(va, vb);
        /* All bytes must match */
        return vminvq_u8(cmp) == 0xFF ? 1 : 0;
    }
    return std::memcmp(a, b, static_cast<size_t>(len)) == 0 ? 1 : 0;
}

const SimdOps g_ops_neon = {
    neon_crc32c,
    neon_memcpy_nt,
    neon_memset_zero,
    neon_ack_bits_scan,
    neon_popcount32,
    neon_replay_check,
    neon_fragment_bitmask_complete,
    neon_fragment_next_missing,
    neon_accumulate_u64,
    neon_addr_equal,
};

} // namespace simd
} // namespace netudp

#else /* Not ARM64 */

namespace netudp { namespace simd {
const SimdOps g_ops_neon = {};
}} // namespace netudp::simd

#endif
