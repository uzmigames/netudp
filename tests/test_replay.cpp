#include <gtest/gtest.h>
#include "../src/crypto/replay.h"

TEST(ReplayProtection, SequentialNoncesPass) {
    netudp::crypto::ReplayProtection rp;

    for (uint64_t i = 1; i <= 100; ++i) {
        EXPECT_FALSE(rp.is_duplicate(i)) << "nonce " << i;
        rp.advance(i);
    }
    EXPECT_EQ(rp.most_recent, 100U);
}

TEST(ReplayProtection, DuplicateRejected) {
    netudp::crypto::ReplayProtection rp;

    rp.advance(42);
    EXPECT_TRUE(rp.is_duplicate(42));
}

TEST(ReplayProtection, TooOldRejected) {
    netudp::crypto::ReplayProtection rp;

    /* Advance to nonce 300 */
    for (uint64_t i = 1; i <= 300; ++i) {
        rp.advance(i);
    }

    /* Nonce 1 is > 256 behind most_recent=300, should be rejected */
    EXPECT_TRUE(rp.is_duplicate(1));

    /* Nonce 44 is 256 behind 300, should be rejected (44 + 256 = 300 <= 300) */
    EXPECT_TRUE(rp.is_duplicate(44));

    /* Nonce 45 is within window (45 + 256 = 301 > 300), but already received */
    EXPECT_TRUE(rp.is_duplicate(45));
}

TEST(ReplayProtection, OutOfOrderWithinWindow) {
    netudp::crypto::ReplayProtection rp;

    /* Receive 1, 2, 3, skip 4, receive 5 */
    rp.advance(1);
    rp.advance(2);
    rp.advance(3);
    rp.advance(5);

    /* 4 was never received — should NOT be duplicate */
    EXPECT_FALSE(rp.is_duplicate(4));
    rp.advance(4);

    /* Now 4 should be duplicate */
    EXPECT_TRUE(rp.is_duplicate(4));
}

TEST(ReplayProtection, FutureNonceAccepted) {
    netudp::crypto::ReplayProtection rp;

    rp.advance(100);
    /* Jump ahead */
    EXPECT_FALSE(rp.is_duplicate(500));
    rp.advance(500);
    EXPECT_EQ(rp.most_recent, 500U);
}

TEST(ReplayProtection, ResetClearsAll) {
    netudp::crypto::ReplayProtection rp;

    for (uint64_t i = 1; i <= 50; ++i) {
        rp.advance(i);
    }

    rp.reset();
    EXPECT_EQ(rp.most_recent, 0U);

    /* After reset, nonce 1 should be accepted again */
    EXPECT_FALSE(rp.is_duplicate(1));
}
