#include "netudp_simd.h"

#if defined(NETUDP_ARCH_X64) || defined(NETUDP_ARCH_X86)

#include <cstring>
#include <immintrin.h> /* AVX2, BMI2 */
#include <nmmintrin.h> /* SSE4.2 for CRC32 */

namespace netudp {
namespace simd {

/* CRC32C — same as SSE4.2 (AVX2 has no CRC32 improvement) */
static uint32_t avx2_crc32c(const uint8_t* data, int len) {
    return g_ops_sse42.crc32c(data, len);
}

static void avx2_memcpy_nt(void* dst, const void* src, size_t len) {
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    size_t i = 0;
    bool aligned = (reinterpret_cast<uintptr_t>(d) & 31) == 0;

    if (aligned) {
        for (; i + 31 < len; i += 32) {
            __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i));
            _mm256_stream_si256(reinterpret_cast<__m256i*>(d + i), v);
        }
        _mm_sfence();
    } else {
        for (; i + 31 < len; i += 32) {
            __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + i), v);
        }
    }

    for (; i < len; ++i) {
        d[i] = s[i];
    }
}

static void avx2_memset_zero(void* dst, size_t len) {
    auto* d = static_cast<uint8_t*>(dst);
    __m256i zero = _mm256_setzero_si256();
    size_t i = 0;
    bool aligned = (reinterpret_cast<uintptr_t>(d) & 31) == 0;

    if (aligned) {
        for (; i + 31 < len; i += 32) {
            _mm256_stream_si256(reinterpret_cast<__m256i*>(d + i), zero);
        }
        _mm_sfence();
    } else {
        for (; i + 31 < len; i += 32) {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + i), zero);
        }
    }

    for (; i < len; ++i) {
        d[i] = 0;
    }
}

static int avx2_ack_bits_scan(uint32_t bits, int* indices) {
    return g_ops_sse42.ack_bits_scan(bits, indices);
}

static int avx2_popcount32(uint32_t v) {
    return g_ops_sse42.popcount32(v);
}

static int avx2_replay_check(const uint64_t* window, uint64_t seq, int size) {
    __m256i target = _mm256_set1_epi64x(static_cast<int64_t>(seq));
    int i = 0;
    for (; i + 3 < size; i += 4) {
        __m256i vals = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(window + i));
        __m256i cmp = _mm256_cmpeq_epi64(vals, target);
        if (_mm256_movemask_epi8(cmp) != 0) {
            return 1;
        }
    }
    /* Tail */
    for (; i < size; ++i) {
        if (window[i] == seq) {
            return 1;
        }
    }
    return 0;
}

static int avx2_fragment_bitmask_complete(const uint8_t* mask, int total) {
    return g_ops_generic.fragment_bitmask_complete(mask, total);
}

static int avx2_fragment_next_missing(const uint8_t* mask, int total) {
    return g_ops_generic.fragment_next_missing(mask, total);
}

static void avx2_accumulate_u64(uint64_t* dst, const uint64_t* src, int count) {
    int i = 0;
    for (; i + 3 < count; i += 4) {
        __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(dst + i));
        __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), _mm256_add_epi64(a, b));
    }
    for (; i < count; ++i) {
        dst[i] += src[i];
    }
}

static int avx2_addr_equal(const void* a, const void* b, int len) {
    if (len >= 16) {
        __m128i va = _mm_loadu_si128(static_cast<const __m128i*>(a));
        __m128i vb = _mm_loadu_si128(static_cast<const __m128i*>(b));
        __m128i cmp = _mm_cmpeq_epi8(va, vb);
        return (_mm_movemask_epi8(cmp) & ((1 << len) - 1)) == ((1 << len) - 1) ? 1 : 0;
    }
    return std::memcmp(a, b, static_cast<size_t>(len)) == 0 ? 1 : 0;
}

const SimdOps g_ops_avx2 = {
    avx2_crc32c,
    avx2_memcpy_nt,
    avx2_memset_zero,
    avx2_ack_bits_scan,
    avx2_popcount32,
    avx2_replay_check,
    avx2_fragment_bitmask_complete,
    avx2_fragment_next_missing,
    avx2_accumulate_u64,
    avx2_addr_equal,
};

} // namespace simd
} // namespace netudp

#else

namespace netudp { namespace simd {
const SimdOps g_ops_avx2 = {};
}} // namespace netudp::simd

#endif
