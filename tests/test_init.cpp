#include <gtest/gtest.h>
#include <netudp/netudp.h>

TEST(Lifecycle, InitReturnsOK) {
    int result = netudp_init();
    EXPECT_EQ(result, NETUDP_OK);
    netudp_term();
}

TEST(Lifecycle, DoubleInitIsIdempotent) {
    EXPECT_EQ(netudp_init(), NETUDP_OK);
    EXPECT_EQ(netudp_init(), NETUDP_OK);
    netudp_term();
}

TEST(Lifecycle, TermAfterInitSucceeds) {
    netudp_init();
    netudp_term();
    /* Should not crash — term is safe after init */
}

TEST(Lifecycle, DoubleTermIsIdempotent) {
    netudp_init();
    netudp_term();
    netudp_term(); /* Second term is a no-op */
}

TEST(Lifecycle, SimdLevelBeforeInitReturnsInvalid) {
    /* Before init, simd_level returns -1 cast to the enum */
    netudp_simd_level_t level = netudp_simd_level();
    EXPECT_EQ(static_cast<int>(level), -1);
}

TEST(Lifecycle, SimdLevelAfterInitReturnsValid) {
    netudp_init();
    netudp_simd_level_t level = netudp_simd_level();
    EXPECT_GE(static_cast<int>(level), 0);
    EXPECT_LE(static_cast<int>(level), 4);
    netudp_term();
}
