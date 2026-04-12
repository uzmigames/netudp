#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include "../src/crypto/packet_crypto.h"
#include "../src/crypto/aead.h"
#include "../src/crypto/random.h"

#include <cstring>

class PacketCryptoTest : public ::testing::Test {
protected:
    void SetUp() override {
        netudp_init();
        /* Set up matching Tx/Rx key pair */
        netudp::crypto::random_bytes(tx_epoch.tx_key, 32);
        std::memcpy(rx_epoch.rx_key, tx_epoch.tx_key, 32);
    }
    void TearDown() override { netudp_term(); }

    netudp::crypto::KeyEpoch tx_epoch;
    netudp::crypto::KeyEpoch rx_epoch;
    uint64_t protocol_id = 0x1234567890ABCDEF;
    uint8_t prefix = 0x14; /* DATA packet, 1-byte seq */
};

TEST_F(PacketCryptoTest, EncryptDecryptRoundTrip) {
    const uint8_t payload[] = "game state update";
    int pt_len = static_cast<int>(sizeof(payload));

    /* First verify raw aead with same key works */
    uint8_t nonce[24] = {};
    uint8_t raw_ct[256] = {};
    uint8_t raw_dec[256] = {};
    int raw_ct_len = netudp::crypto::aead_encrypt(tx_epoch.tx_key, nonce, nullptr, 0,
                                                   payload, pt_len, raw_ct);
    ASSERT_EQ(raw_ct_len, pt_len + 16);
    int raw_dec_len = netudp::crypto::aead_decrypt(rx_epoch.rx_key, nonce, nullptr, 0,
                                                    raw_ct, raw_ct_len, raw_dec);
    ASSERT_EQ(raw_dec_len, pt_len) << "Raw AEAD failed — keys don't match";

    /* Now test packet encrypt/decrypt */
    uint8_t ct[256] = {};
    int ct_len = netudp::crypto::packet_encrypt(&tx_epoch, protocol_id, prefix,
                                                 payload, pt_len, ct);
    ASSERT_EQ(ct_len, pt_len + 16);
    EXPECT_EQ(tx_epoch.tx_nonce_counter, 1U);

    uint8_t dec[256] = {};
    int dec_len = netudp::crypto::packet_decrypt(&rx_epoch, protocol_id, prefix,
                                                  0 /* nonce_counter */, ct, ct_len, dec);
    ASSERT_EQ(dec_len, pt_len);
    EXPECT_EQ(std::memcmp(dec, payload, static_cast<size_t>(pt_len)), 0);
}

TEST_F(PacketCryptoTest, ReplayDetection) {
    const uint8_t payload[] = "test";
    uint8_t ct[128] = {};
    int ct_len = netudp::crypto::packet_encrypt(&tx_epoch, protocol_id, prefix,
                                                 payload, sizeof(payload), ct);
    ASSERT_GT(ct_len, 0);

    uint8_t dec[128] = {};
    /* First decrypt succeeds */
    int r1 = netudp::crypto::packet_decrypt(&rx_epoch, protocol_id, prefix,
                                             0, ct, ct_len, dec);
    ASSERT_GT(r1, 0);

    /* Second decrypt with same nonce fails (replay) */
    int r2 = netudp::crypto::packet_decrypt(&rx_epoch, protocol_id, prefix,
                                             0, ct, ct_len, dec);
    EXPECT_EQ(r2, -1);
}

TEST_F(PacketCryptoTest, AADMismatchRejected) {
    const uint8_t payload[] = "secret";
    uint8_t ct[128] = {};
    int ct_len = netudp::crypto::packet_encrypt(&tx_epoch, protocol_id, prefix,
                                                 payload, sizeof(payload), ct);

    uint8_t dec[128] = {};
    /* Wrong prefix byte = different AAD */
    int result = netudp::crypto::packet_decrypt(&rx_epoch, protocol_id, 0xFF,
                                                 0, ct, ct_len, dec);
    EXPECT_EQ(result, -1);
}

TEST_F(PacketCryptoTest, NonceCounterIncrements) {
    const uint8_t payload[] = "p";
    uint8_t ct[64] = {};

    for (int i = 0; i < 5; ++i) {
        netudp::crypto::packet_encrypt(&tx_epoch, protocol_id, prefix,
                                        payload, sizeof(payload), ct);
    }
    EXPECT_EQ(tx_epoch.tx_nonce_counter, 5U);
    EXPECT_GT(tx_epoch.bytes_transmitted, 0U);
}

TEST_F(PacketCryptoTest, MultiplePacketsSequential) {
    for (uint64_t i = 0; i < 10; ++i) {
        uint8_t payload[32] = {};
        payload[0] = static_cast<uint8_t>(i);

        uint8_t ct[64] = {};
        int ct_len = netudp::crypto::packet_encrypt(&tx_epoch, protocol_id, prefix,
                                                     payload, 32, ct);
        ASSERT_GT(ct_len, 0);

        uint8_t dec[64] = {};
        int dec_len = netudp::crypto::packet_decrypt(&rx_epoch, protocol_id, prefix,
                                                      i, ct, ct_len, dec);
        ASSERT_EQ(dec_len, 32);
        EXPECT_EQ(dec[0], static_cast<uint8_t>(i));
    }
}
