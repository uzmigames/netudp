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
    int level = netudp_simd_level();
    EXPECT_EQ(level, -1);
}

TEST(Lifecycle, SimdLevelAfterInitReturnsValid) {
    netudp_init();
    int level = netudp_simd_level();
    EXPECT_GE(level, 0);
    EXPECT_LE(level, 4);
    netudp_term();
}
