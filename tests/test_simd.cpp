#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include "../src/simd/netudp_simd.h"

#include <cstring>

class SimdTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

TEST_F(SimdTest, DetectionReturnsValidLevel) {
    int level = netudp_simd_level();
    EXPECT_GE(level, 0);
    EXPECT_LE(level, 4);
}

TEST_F(SimdTest, DispatchTableIsSet) {
    EXPECT_NE(netudp::simd::g_simd, nullptr);
}

TEST_F(SimdTest, CRC32CKnownVector) {
    /* "123456789" → CRC32C = 0xE3069283 */
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint32_t crc = netudp::simd::g_simd->crc32c(data, 9);
    EXPECT_EQ(crc, 0xE3069283U);
}

TEST_F(SimdTest, CRC32CEmpty) {
    uint32_t crc = netudp::simd::g_simd->crc32c(nullptr, 0);
    EXPECT_EQ(crc, 0x00000000U);
}

TEST_F(SimdTest, MemcpyNtCopiesCorrectly) {
    uint8_t src[256];
    uint8_t dst[256];
    for (int i = 0; i < 256; ++i) {
        src[i] = static_cast<uint8_t>(i);
    }
    netudp::simd::g_simd->memcpy_nt(dst, src, 256);
    EXPECT_EQ(std::memcmp(src, dst, 256), 0);
}

TEST_F(SimdTest, MemsetZeroClearsBuffer) {
    uint8_t buf[128];
    std::memset(buf, 0xFF, 128);
    netudp::simd::g_simd->memset_zero(buf, 128);
    for (int i = 0; i < 128; ++i) {
        EXPECT_EQ(buf[i], 0) << "byte " << i;
    }
}

TEST_F(SimdTest, AckBitsScanFindsSetBits) {
    int indices[32] = {};
    /* bits: 0b10100101 = positions 0, 2, 5, 7 */
    int count = netudp::simd::g_simd->ack_bits_scan(0xA5, indices);
    EXPECT_EQ(count, 4);
    EXPECT_EQ(indices[0], 0);
    EXPECT_EQ(indices[1], 2);
    EXPECT_EQ(indices[2], 5);
    EXPECT_EQ(indices[3], 7);
}

TEST_F(SimdTest, AckBitsScanEmpty) {
    int indices[32] = {};
    int count = netudp::simd::g_simd->ack_bits_scan(0, indices);
    EXPECT_EQ(count, 0);
}

TEST_F(SimdTest, Popcount32) {
    EXPECT_EQ(netudp::simd::g_simd->popcount32(0), 0);
    EXPECT_EQ(netudp::simd::g_simd->popcount32(1), 1);
    EXPECT_EQ(netudp::simd::g_simd->popcount32(0xFFFFFFFF), 32);
    EXPECT_EQ(netudp::simd::g_simd->popcount32(0xA5A5A5A5), 16);
}

TEST_F(SimdTest, ReplayCheckFindsDuplicate) {
    uint64_t window[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    EXPECT_EQ(netudp::simd::g_simd->replay_check(window, 30, 8), 1);
    EXPECT_EQ(netudp::simd::g_simd->replay_check(window, 99, 8), 0);
}

TEST_F(SimdTest, FragmentBitmaskComplete) {
    uint8_t mask[32] = {};
    /* Set all 8 bits for 8 fragments */
    mask[0] = 0xFF;
    EXPECT_EQ(netudp::simd::g_simd->fragment_bitmask_complete(mask, 8), 1);
    /* Clear one */
    mask[0] = 0xFE;
    EXPECT_EQ(netudp::simd::g_simd->fragment_bitmask_complete(mask, 8), 0);
}

TEST_F(SimdTest, FragmentNextMissing) {
    uint8_t mask[32] = {};
    mask[0] = 0xFF;
    mask[1] = 0xFB; /* bit 2 of byte 1 = fragment 10 is missing */
    EXPECT_EQ(netudp::simd::g_simd->fragment_next_missing(mask, 16), 10);
}

TEST_F(SimdTest, AccumulateU64) {
    uint64_t dst[4] = {10, 20, 30, 40};
    uint64_t src[4] = {1, 2, 3, 4};
    netudp::simd::g_simd->accumulate_u64(dst, src, 4);
    EXPECT_EQ(dst[0], 11U);
    EXPECT_EQ(dst[1], 22U);
    EXPECT_EQ(dst[2], 33U);
    EXPECT_EQ(dst[3], 44U);
}

TEST_F(SimdTest, AddrEqual) {
    uint8_t a[16] = {192, 168, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t b[16] = {192, 168, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t c[16] = {192, 168, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ(netudp::simd::g_simd->addr_equal(a, b, 4), 1);
    EXPECT_EQ(netudp::simd::g_simd->addr_equal(a, c, 4), 0);
}
