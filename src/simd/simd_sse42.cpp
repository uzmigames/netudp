#include "netudp_simd.h"

#if defined(NETUDP_ARCH_X64) || defined(NETUDP_ARCH_X86)

#include <cstring>
#include <nmmintrin.h> /* SSE4.2 */
#include <immintrin.h> /* POPCNT */

namespace netudp {
namespace simd {

static uint32_t sse42_crc32c(const uint8_t* data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    int i = 0;

#if defined(NETUDP_ARCH_X64)
    /* Process 8 bytes at a time with _mm_crc32_u64 */
    for (; i + 7 < len; i += 8) {
        uint64_t val = 0;
        std::memcpy(&val, data + i, 8);
        crc = static_cast<uint32_t>(_mm_crc32_u64(crc, val));
    }
#endif

    /* Process 4 bytes */
    for (; i + 3 < len; i += 4) {
        uint32_t val = 0;
        std::memcpy(&val, data + i, 4);
        crc = _mm_crc32_u32(crc, val);
    }

    /* Process remaining bytes */
    for (; i < len; ++i) {
        crc = _mm_crc32_u8(crc, data[i]);
    }

    return crc ^ 0xFFFFFFFF;
}

static void sse42_memcpy_nt(void* dst, const void* src, size_t len) {
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    size_t i = 0;
    bool aligned = (reinterpret_cast<uintptr_t>(d) & 15) == 0;

    if (aligned) {
        for (; i + 15 < len; i += 16) {
            __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + i));
            _mm_stream_si128(reinterpret_cast<__m128i*>(d + i), v);
        }
        _mm_sfence();
    } else {
        for (; i + 15 < len; i += 16) {
            __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + i));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d + i), v);
        }
    }

    for (; i < len; ++i) {
        d[i] = s[i];
    }
}

static void sse42_memset_zero(void* dst, size_t len) {
    auto* d = static_cast<uint8_t*>(dst);
    __m128i zero = _mm_setzero_si128();
    size_t i = 0;
    bool aligned = (reinterpret_cast<uintptr_t>(d) & 15) == 0;

    if (aligned) {
        for (; i + 15 < len; i += 16) {
            _mm_stream_si128(reinterpret_cast<__m128i*>(d + i), zero);
        }
        _mm_sfence();
    } else {
        for (; i + 15 < len; i += 16) {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d + i), zero);
        }
    }

    for (; i < len; ++i) {
        d[i] = 0;
    }
}

static int sse42_ack_bits_scan(uint32_t bits, int* indices) {
    int count = 0;
    while (bits != 0) {
        int idx = static_cast<int>(_tzcnt_u32(bits));
        indices[count++] = idx;
        bits &= bits - 1; /* Clear lowest set bit */
    }
    return count;
}

static int sse42_popcount32(uint32_t v) {
    return static_cast<int>(_mm_popcnt_u32(v));
}

static int sse42_replay_check(const uint64_t* window, uint64_t seq, int size) {
    __m128i target = _mm_set1_epi64x(static_cast<int64_t>(seq));
    int i = 0;
    for (; i + 1 < size; i += 2) {
        __m128i vals = _mm_loadu_si128(reinterpret_cast<const __m128i*>(window + i));
        __m128i cmp = _mm_cmpeq_epi64(vals, target);
        if (_mm_movemask_epi8(cmp) != 0) {
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

/* Fragment and accumulate use generic — SSE4.2 has no special advantage here */
static int sse42_fragment_bitmask_complete(const uint8_t* mask, int total) {
    return g_ops_generic.fragment_bitmask_complete(mask, total);
}

static int sse42_fragment_next_missing(const uint8_t* mask, int total) {
    return g_ops_generic.fragment_next_missing(mask, total);
}

static void sse42_accumulate_u64(uint64_t* dst, const uint64_t* src, int count) {
    int i = 0;
    for (; i + 1 < count; i += 2) {
        __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst + i));
        __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), _mm_add_epi64(a, b));
    }
    for (; i < count; ++i) {
        dst[i] += src[i];
    }
}

static int sse42_addr_equal(const void* a, const void* b, int len) {
    if (len >= 16) {
        __m128i va = _mm_loadu_si128(static_cast<const __m128i*>(a));
        __m128i vb = _mm_loadu_si128(static_cast<const __m128i*>(b));
        __m128i cmp = _mm_cmpeq_epi8(va, vb);
        return (_mm_movemask_epi8(cmp) & ((1 << len) - 1)) == ((1 << len) - 1) ? 1 : 0;
    }
    return std::memcmp(a, b, static_cast<size_t>(len)) == 0 ? 1 : 0;
}

const SimdOps g_ops_sse42 = {
    sse42_crc32c,
    sse42_memcpy_nt,
    sse42_memset_zero,
    sse42_ack_bits_scan,
    sse42_popcount32,
    sse42_replay_check,
    sse42_fragment_bitmask_complete,
    sse42_fragment_next_missing,
    sse42_accumulate_u64,
    sse42_addr_equal,
};

} // namespace simd
} // namespace netudp

#else /* Not x86 — provide empty table, won't be used */

namespace netudp { namespace simd {
const SimdOps g_ops_sse42 = {};
}} // namespace netudp::simd

#endif
