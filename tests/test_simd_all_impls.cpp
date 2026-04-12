#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include "../src/simd/netudp_simd.h"

#include <cstring>

/**
 * Test ALL SIMD implementations (generic, SSE4.2, AVX2) directly
 * regardless of CPU detection. This ensures full coverage of all ISA paths.
 */

class SimdImplTest : public ::testing::TestWithParam<const netudp::simd::SimdOps*> {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

TEST_P(SimdImplTest, CRC32CKnownVector) {
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint32_t crc = GetParam()->crc32c(data, 9);
    EXPECT_EQ(crc, 0xE3069283U);
}

TEST_P(SimdImplTest, CRC32CEmpty) {
    EXPECT_EQ(GetParam()->crc32c(nullptr, 0), 0x00000000U);
}

TEST_P(SimdImplTest, MemcpyCopies) {
    uint8_t src[256];
    uint8_t dst[256];
    for (int i = 0; i < 256; ++i) {
        src[i] = static_cast<uint8_t>(i);
    }
    std::memset(dst, 0, 256);
    GetParam()->memcpy_nt(dst, src, 256);
    EXPECT_EQ(std::memcmp(src, dst, 256), 0);
}

TEST_P(SimdImplTest, MemcpySmall) {
    uint8_t src[7] = {1, 2, 3, 4, 5, 6, 7};
    uint8_t dst[7] = {};
    GetParam()->memcpy_nt(dst, src, 7);
    EXPECT_EQ(std::memcmp(src, dst, 7), 0);
}

TEST_P(SimdImplTest, MemsetZero) {
    uint8_t buf[256];
    std::memset(buf, 0xFF, 256);
    GetParam()->memset_zero(buf, 256);
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(buf[i], 0) << "byte " << i;
    }
}

TEST_P(SimdImplTest, MemsetZeroSmall) {
    uint8_t buf[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    GetParam()->memset_zero(buf, 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(buf[i], 0);
    }
}

TEST_P(SimdImplTest, AckBitsScan) {
    int indices[32] = {};
    int count = GetParam()->ack_bits_scan(0xA5, indices);
    EXPECT_EQ(count, 4);
    EXPECT_EQ(indices[0], 0);
    EXPECT_EQ(indices[1], 2);
    EXPECT_EQ(indices[2], 5);
    EXPECT_EQ(indices[3], 7);
}

TEST_P(SimdImplTest, AckBitsScanAll) {
    int indices[32] = {};
    int count = GetParam()->ack_bits_scan(0xFFFFFFFF, indices);
    EXPECT_EQ(count, 32);
}

TEST_P(SimdImplTest, AckBitsScanEmpty) {
    int indices[32] = {};
    EXPECT_EQ(GetParam()->ack_bits_scan(0, indices), 0);
}

TEST_P(SimdImplTest, Popcount) {
    EXPECT_EQ(GetParam()->popcount32(0), 0);
    EXPECT_EQ(GetParam()->popcount32(1), 1);
    EXPECT_EQ(GetParam()->popcount32(0xFF), 8);
    EXPECT_EQ(GetParam()->popcount32(0xFFFFFFFF), 32);
    EXPECT_EQ(GetParam()->popcount32(0xA5A5A5A5), 16);
}

TEST_P(SimdImplTest, ReplayCheckFound) {
    uint64_t window[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    EXPECT_EQ(GetParam()->replay_check(window, 30, 8), 1);
    EXPECT_EQ(GetParam()->replay_check(window, 80, 8), 1);
}

TEST_P(SimdImplTest, ReplayCheckNotFound) {
    uint64_t window[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    EXPECT_EQ(GetParam()->replay_check(window, 99, 8), 0);
    EXPECT_EQ(GetParam()->replay_check(window, 0, 8), 0);
}

TEST_P(SimdImplTest, ReplayCheckOddSize) {
    uint64_t window[3] = {100, 200, 300};
    EXPECT_EQ(GetParam()->replay_check(window, 200, 3), 1);
    EXPECT_EQ(GetParam()->replay_check(window, 999, 3), 0);
}

TEST_P(SimdImplTest, FragmentComplete) {
    uint8_t mask[32] = {};
    mask[0] = 0xFF;
    EXPECT_EQ(GetParam()->fragment_bitmask_complete(mask, 8), 1);
    mask[0] = 0xFE;
    EXPECT_EQ(GetParam()->fragment_bitmask_complete(mask, 8), 0);
}

TEST_P(SimdImplTest, FragmentNextMissing) {
    uint8_t mask[32] = {};
    mask[0] = 0xFF;
    mask[1] = 0xFB;
    EXPECT_EQ(GetParam()->fragment_next_missing(mask, 16), 10);
}

TEST_P(SimdImplTest, FragmentNoneMissing) {
    uint8_t mask[32];
    std::memset(mask, 0xFF, 32);
    EXPECT_EQ(GetParam()->fragment_next_missing(mask, 32), -1);
}

TEST_P(SimdImplTest, Accumulate) {
    uint64_t dst[4] = {10, 20, 30, 40};
    uint64_t src[4] = {1, 2, 3, 4};
    GetParam()->accumulate_u64(dst, src, 4);
    EXPECT_EQ(dst[0], 11U);
    EXPECT_EQ(dst[1], 22U);
    EXPECT_EQ(dst[2], 33U);
    EXPECT_EQ(dst[3], 44U);
}

TEST_P(SimdImplTest, AccumulateOddCount) {
    uint64_t dst[3] = {100, 200, 300};
    uint64_t src[3] = {10, 20, 30};
    GetParam()->accumulate_u64(dst, src, 3);
    EXPECT_EQ(dst[0], 110U);
    EXPECT_EQ(dst[1], 220U);
    EXPECT_EQ(dst[2], 330U);
}

TEST_P(SimdImplTest, AddrEqual) {
    uint8_t a[16] = {10, 0, 0, 1};
    uint8_t b[16] = {10, 0, 0, 1};
    uint8_t c[16] = {10, 0, 0, 2};
    EXPECT_EQ(GetParam()->addr_equal(a, b, 4), 1);
    EXPECT_EQ(GetParam()->addr_equal(a, c, 4), 0);
}

TEST_P(SimdImplTest, AddrEqual16) {
    uint8_t a[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8_t b[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8_t c[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    EXPECT_EQ(GetParam()->addr_equal(a, b, 16), 1);
    EXPECT_EQ(GetParam()->addr_equal(a, c, 16), 0);
}

/* Instantiate for all available ISAs */
INSTANTIATE_TEST_SUITE_P(Generic, SimdImplTest,
    ::testing::Values(&netudp::simd::g_ops_generic));

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
INSTANTIATE_TEST_SUITE_P(SSE42, SimdImplTest,
    ::testing::Values(&netudp::simd::g_ops_sse42));

INSTANTIATE_TEST_SUITE_P(AVX2, SimdImplTest,
    ::testing::Values(&netudp::simd::g_ops_avx2));
#endif
