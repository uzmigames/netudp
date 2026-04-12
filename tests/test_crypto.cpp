#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include "../src/crypto/aead.h"
#include "../src/crypto/xchacha.h"
#include "../src/crypto/crc32c.h"
#include "../src/crypto/random.h"

#include <cstring>

class CryptoTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

/* ===== AEAD ChaCha20-Poly1305 ===== */

TEST_F(CryptoTest, AeadRoundTrip) {
    uint8_t key[32] = {};
    netudp::crypto::random_bytes(key, 32);

    uint8_t nonce[24] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t aad[] = "netudp_test_aad";
    const uint8_t plaintext[] = "Hello, encrypted world!";
    int pt_len = static_cast<int>(sizeof(plaintext));

    uint8_t ciphertext[256] = {};
    int ct_len = netudp::crypto::aead_encrypt(key, nonce, aad, sizeof(aad),
                                               plaintext, pt_len, ciphertext);
    ASSERT_EQ(ct_len, pt_len + 16);

    uint8_t decrypted[256] = {};
    int dec_len = netudp::crypto::aead_decrypt(key, nonce, aad, sizeof(aad),
                                                ciphertext, ct_len, decrypted);
    ASSERT_EQ(dec_len, pt_len);
    EXPECT_EQ(std::memcmp(decrypted, plaintext, static_cast<size_t>(pt_len)), 0);
}

TEST_F(CryptoTest, AeadTamperedCiphertext) {
    uint8_t key[32] = {};
    netudp::crypto::random_bytes(key, 32);
    uint8_t nonce[24] = {};

    const uint8_t pt[] = "secret data";
    uint8_t ct[128] = {};
    int ct_len = netudp::crypto::aead_encrypt(key, nonce, nullptr, 0,
                                               pt, sizeof(pt), ct);
    ASSERT_GT(ct_len, 0);

    /* Flip one bit in ciphertext */
    ct[0] ^= 0x01;

    uint8_t dec[128] = {};
    int result = netudp::crypto::aead_decrypt(key, nonce, nullptr, 0,
                                               ct, ct_len, dec);
    EXPECT_EQ(result, -1); /* Auth failure */
}

TEST_F(CryptoTest, AeadWrongKey) {
    uint8_t key1[32] = {};
    uint8_t key2[32] = {};
    netudp::crypto::random_bytes(key1, 32);
    netudp::crypto::random_bytes(key2, 32);
    uint8_t nonce[24] = {};

    const uint8_t pt[] = "test";
    uint8_t ct[128] = {};
    int ct_len = netudp::crypto::aead_encrypt(key1, nonce, nullptr, 0,
                                               pt, sizeof(pt), ct);

    uint8_t dec[128] = {};
    int result = netudp::crypto::aead_decrypt(key2, nonce, nullptr, 0,
                                               ct, ct_len, dec);
    EXPECT_EQ(result, -1);
}

TEST_F(CryptoTest, AeadWrongAAD) {
    uint8_t key[32] = {};
    netudp::crypto::random_bytes(key, 32);
    uint8_t nonce[24] = {};

    const uint8_t aad1[] = "correct_aad";
    const uint8_t aad2[] = "wrong___aad";
    const uint8_t pt[] = "data";

    uint8_t ct[128] = {};
    int ct_len = netudp::crypto::aead_encrypt(key, nonce, aad1, sizeof(aad1),
                                               pt, sizeof(pt), ct);

    uint8_t dec[128] = {};
    int result = netudp::crypto::aead_decrypt(key, nonce, aad2, sizeof(aad2),
                                               ct, ct_len, dec);
    EXPECT_EQ(result, -1);
}

/* ===== XChaCha20-Poly1305 ===== */

TEST_F(CryptoTest, XChaChaRoundTrip) {
    uint8_t key[32] = {};
    netudp::crypto::random_bytes(key, 32);

    uint8_t nonce[24] = {};
    netudp::crypto::random_bytes(nonce, 24);

    const uint8_t pt[] = "connect token private data for xchacha20";
    int pt_len = static_cast<int>(sizeof(pt));

    uint8_t ct[256] = {};
    int ct_len = netudp::crypto::xchacha_encrypt(key, nonce, nullptr, 0,
                                                  pt, pt_len, ct);
    ASSERT_EQ(ct_len, pt_len + 16);

    uint8_t dec[256] = {};
    int dec_len = netudp::crypto::xchacha_decrypt(key, nonce, nullptr, 0,
                                                   ct, ct_len, dec);
    ASSERT_EQ(dec_len, pt_len);
    EXPECT_EQ(std::memcmp(dec, pt, static_cast<size_t>(pt_len)), 0);
}

TEST_F(CryptoTest, XChachaTampered) {
    uint8_t key[32] = {};
    uint8_t nonce[24] = {};
    netudp::crypto::random_bytes(key, 32);
    netudp::crypto::random_bytes(nonce, 24);

    const uint8_t pt[] = "secret";
    uint8_t ct[128] = {};
    int ct_len = netudp::crypto::xchacha_encrypt(key, nonce, nullptr, 0,
                                                  pt, sizeof(pt), ct);
    ct[2] ^= 0xFF;

    uint8_t dec[128] = {};
    EXPECT_EQ(netudp::crypto::xchacha_decrypt(key, nonce, nullptr, 0,
                                               ct, ct_len, dec), -1);
}

/* ===== CRC32C ===== */

TEST_F(CryptoTest, CRC32CKnownVector) {
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(netudp::crypto::crc32c(data, 9), 0xE3069283U);
}

/* ===== CSPRNG ===== */

TEST_F(CryptoTest, RandomBytesNonZero) {
    uint8_t buf[64] = {};
    netudp::crypto::random_bytes(buf, 64);

    /* Check that at least some bytes are non-zero (vanishingly unlikely all zero) */
    int nonzero = 0;
    for (int i = 0; i < 64; ++i) {
        if (buf[i] != 0) {
            ++nonzero;
        }
    }
    EXPECT_GT(nonzero, 10);
}

TEST_F(CryptoTest, RandomBytesDifferentCalls) {
    uint8_t a[32] = {};
    uint8_t b[32] = {};
    netudp::crypto::random_bytes(a, 32);
    netudp::crypto::random_bytes(b, 32);
    EXPECT_NE(std::memcmp(a, b, 32), 0);
}
