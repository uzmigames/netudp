#include <gtest/gtest.h>
#include "../src/crypto/packet_crypto.h"
#include "../src/crypto/aead.h"
#include <cstring>

using namespace netudp::crypto;

/* ======================================================================
 * Helpers
 * ====================================================================== */

static void fill_key(uint8_t key[32], uint8_t val) {
    std::memset(key, val, 32);
}

static KeyEpoch make_epoch(uint8_t tx_fill = 0xAA, uint8_t rx_fill = 0xBB) {
    KeyEpoch e{};
    fill_key(e.tx_key, tx_fill);
    fill_key(e.rx_key, rx_fill);
    e.epoch_start_time = 1000.0; /* arbitrary start */
    return e;
}

/* ======================================================================
 * Tests
 * ====================================================================== */

/* 3.1 — Rekey detection */

TEST(Rekey, ShouldRekeyNonceTreshold) {
    KeyEpoch e = make_epoch();
    e.tx_nonce_counter = REKEY_NONCE_THRESHOLD - 1;
    EXPECT_FALSE(should_rekey(e, 1001.0));

    e.tx_nonce_counter = REKEY_NONCE_THRESHOLD;
    EXPECT_TRUE(should_rekey(e, 1001.0));
}

TEST(Rekey, ShouldRekeyBytesThreshold) {
    KeyEpoch e = make_epoch();
    e.bytes_transmitted = REKEY_BYTES_THRESHOLD - 1;
    EXPECT_FALSE(should_rekey(e, 1001.0));

    e.bytes_transmitted = REKEY_BYTES_THRESHOLD;
    EXPECT_TRUE(should_rekey(e, 1001.0));
}

TEST(Rekey, ShouldRekeyEpochDuration) {
    KeyEpoch e = make_epoch();
    e.epoch_start_time = 1.0; /* non-zero so the > 0.0 guard passes */
    EXPECT_FALSE(should_rekey(e, 1.0 + REKEY_EPOCH_SECONDS - 1.0));
    EXPECT_TRUE(should_rekey(e, 1.0 + REKEY_EPOCH_SECONDS));
}

/* 3.2 — REKEY flag constant is correct */

TEST(Rekey, DataRekeyPrefixIsBit3SetOnData) {
    /* DATA = 0x04, REKEY_FLAG = 0x08, DATA_REKEY = 0x0C */
    EXPECT_EQ(PACKET_PREFIX_DATA_REKEY, static_cast<uint8_t>(0x0C));
    EXPECT_EQ(PACKET_PREFIX_DATA_REKEY & 0x04U, static_cast<uint8_t>(0x04));
    EXPECT_EQ(PACKET_PREFIX_DATA_REKEY & 0x08U, static_cast<uint8_t>(0x08));
}

/* 3.3 — Key derivation: new keys differ from old, epoch_number increments */

TEST(Rekey, ActivateRekeyDerivesNewKeys) {
    KeyEpoch e = make_epoch();
    uint8_t old_tx[32];
    uint8_t old_rx[32];
    std::memcpy(old_tx, e.tx_key, 32);
    std::memcpy(old_rx, e.rx_key, 32);

    uint32_t old_epoch = e.epoch_number;

    prepare_rekey(e);
    activate_rekey(e, 2000.0);

    EXPECT_EQ(e.epoch_number, old_epoch + 1U);
    EXPECT_NE(std::memcmp(e.tx_key, old_tx, 32), 0) << "tx_key must change after rekey";
    EXPECT_NE(std::memcmp(e.rx_key, old_rx, 32), 0) << "rx_key must change after rekey";
    EXPECT_EQ(e.tx_nonce_counter, 0U);
    EXPECT_EQ(e.bytes_transmitted, 0U);
    EXPECT_EQ(e.epoch_start_time, 2000.0);
}

TEST(Rekey, SenderAndReceiverDeriveSameKeys) {
    /* Sender and receiver start with the same key material (initial handshake).
     * After rekey both sides must agree on new keys.                           */
    KeyEpoch sender = make_epoch(0x11, 0x22);
    KeyEpoch receiver = make_epoch(0x22, 0x11); /* rx/tx swapped for receiver  */

    /* Sender initiates rekey */
    prepare_rekey(sender);
    activate_rekey(sender, 5000.0);

    /* Receiver mirrors the same derivation */
    on_receive_rekey(receiver, 5000.0);

    /* sender's new tx_key == receiver's new rx_key */
    EXPECT_EQ(std::memcmp(sender.tx_key, receiver.rx_key, 32), 0)
        << "sender new tx_key must equal receiver new rx_key";

    /* sender's new rx_key == receiver's new tx_key */
    EXPECT_EQ(std::memcmp(sender.rx_key, receiver.tx_key, 32), 0)
        << "sender new rx_key must equal receiver new tx_key";
}

/* 3.4 — Grace window */

TEST(Rekey, GraceWindowAcceptsOldKeyPackets) {
    KeyEpoch e = make_epoch();

    /* Save old rx_key */
    uint8_t old_rx[32];
    std::memcpy(old_rx, e.rx_key, 32);

    /* Rekey — old_rx_key is preserved in e.old_rx_key */
    prepare_rekey(e);
    activate_rekey(e, 9000.0);

    EXPECT_EQ(e.grace_packets_remaining, REKEY_GRACE_PACKETS);
    EXPECT_EQ(std::memcmp(e.old_rx_key, old_rx, 32), 0)
        << "old_rx_key must be saved for grace window";

    /* Encrypt a fake packet with the old rx_key (as if sender used old key) */
    static constexpr uint64_t kProtocolId = 0xDEADBEEF12345678ULL;
    static constexpr uint8_t  kPrefix     = 0x04U;

    uint8_t pt_in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t ct[8 + 24]; /* pt + tag */

    /* Build nonce and AAD manually to encrypt with old_rx_key */
    uint8_t nonce[24] = {};
    uint64_t nonce_val = 42;
    std::memcpy(nonce, &nonce_val, 8);

    uint8_t aad[22];
    build_aad(kProtocolId, kPrefix, aad);

    int ct_len = aead_encrypt(old_rx, nonce, aad, 22, pt_in, 8, ct);
    ASSERT_EQ(ct_len, 24);

    /* packet_decrypt with NEW rx_key should FAIL */
    uint8_t pt_out[32] = {};
    int rc = packet_decrypt(&e, kProtocolId, kPrefix, nonce_val, ct, ct_len, pt_out);
    EXPECT_LT(rc, 0) << "new rx_key should not decrypt old-key packet";

    /* packet_decrypt_grace with OLD rx_key should SUCCEED */
    int grace_len = packet_decrypt_grace(&e, kProtocolId, kPrefix, nonce_val,
                                          ct, ct_len, pt_out);
    EXPECT_EQ(grace_len, 8);
    EXPECT_EQ(std::memcmp(pt_out, pt_in, 8), 0);

    /* grace_packets_remaining must have decremented */
    EXPECT_EQ(e.grace_packets_remaining, REKEY_GRACE_PACKETS - 1);
}

TEST(Rekey, PostGraceRejectsOldKeyPackets) {
    KeyEpoch e = make_epoch();

    prepare_rekey(e);
    activate_rekey(e, 9000.0);

    /* Exhaust the grace window */
    e.grace_packets_remaining = 0;

    /* Grace decrypt must fail immediately */
    uint8_t ct[24] = {};
    uint8_t pt[8]  = {};
    int rc = packet_decrypt_grace(&e, 0xAABBCCDD11223344ULL, 0x04U, 0ULL,
                                   ct, 24, pt);
    EXPECT_LT(rc, 0) << "grace decrypt must fail after window exhausted";
}
