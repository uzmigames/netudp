#include <gtest/gtest.h>
#include "../src/compress/compressor.h"

#include <cstring>

/**
 * Tests for the Compressor wrapper.
 * Without NETUDP_HAS_NETC, compression is passthrough (no-op).
 * With NETUDP_HAS_NETC, real netc compression is used (tested separately).
 */

TEST(Compressor, NoneMode) {
    netudp::Compressor comp;
    EXPECT_TRUE(comp.init(netudp::CompressionMode::None, nullptr));
    EXPECT_FALSE(comp.enabled());
    EXPECT_EQ(comp.mode(), netudp::CompressionMode::None);
}

TEST(Compressor, PassthroughWithoutDict) {
    netudp::Compressor comp;
    /* Stateful mode but null dict → falls back to None */
    EXPECT_TRUE(comp.init(netudp::CompressionMode::Stateful, nullptr));
    EXPECT_EQ(comp.mode(), netudp::CompressionMode::None);
}

TEST(Compressor, PassthroughCompress) {
    netudp::Compressor comp;
    comp.init(netudp::CompressionMode::None, nullptr);

    uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t out[32] = {};
    bool compressed = true;

    int len = comp.compress(data, 8, out, 32, &compressed);
    EXPECT_EQ(len, 8);
    EXPECT_FALSE(compressed);
    EXPECT_EQ(std::memcmp(out, data, 8), 0);
}

TEST(Compressor, PassthroughDecompress) {
    netudp::Compressor comp;
    comp.init(netudp::CompressionMode::None, nullptr);

    uint8_t data[] = {10, 20, 30};
    uint8_t out[32] = {};

    int len = comp.decompress(data, 3, out, 32);
    EXPECT_EQ(len, 3);
    EXPECT_EQ(std::memcmp(out, data, 3), 0);
}

TEST(Compressor, EmptyInput) {
    netudp::Compressor comp;
    comp.init(netudp::CompressionMode::None, nullptr);

    uint8_t out[32] = {};
    bool compressed = true;
    int len = comp.compress(nullptr, 0, out, 32, &compressed);
    EXPECT_EQ(len, 0);
    EXPECT_FALSE(compressed);
}

#if defined(NETUDP_HAS_NETC)
/* These tests only run when netc is actually linked */

TEST(Compressor, StatefulWithNetc) {
    /* Would need a trained dictionary — skip for unit tests.
     * Integration tests with real netc dict are in Phase 6. */
}

#endif
