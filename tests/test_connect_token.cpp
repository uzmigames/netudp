#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>
#include "../src/connection/connect_token.h"
#include "../src/crypto/random.h"

#include <cstring>
#include <ctime>

class ConnectTokenTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

TEST_F(ConnectTokenTest, GenerateAndValidate) {
    uint8_t private_key[32];
    netudp::crypto::random_bytes(private_key, 32);

    uint8_t user_data[256] = {};
    user_data[0] = 0xAB;
    user_data[255] = 0xCD;

    const char* servers[] = {"127.0.0.1:27015"};
    uint8_t token[2048] = {};

    int result = netudp_generate_connect_token(
        1, servers, 300, 10,
        12345, 0xDEADBEEF,
        private_key, user_data, token
    );
    ASSERT_EQ(result, NETUDP_OK);

    /* Validate */
    netudp_address_t server_addr = {};
    netudp_parse_address("127.0.0.1:27015", &server_addr);

    uint64_t now = static_cast<uint64_t>(std::time(nullptr));

    netudp::PrivateConnectToken priv = {};
    int val_result = netudp::validate_connect_token(
        token, 0xDEADBEEF, private_key, now, &server_addr, &priv
    );
    ASSERT_EQ(val_result, NETUDP_OK);
    EXPECT_EQ(priv.client_id, 12345U);
    EXPECT_EQ(priv.timeout_seconds, 10U);
    EXPECT_EQ(priv.num_server_addresses, 1U);
    EXPECT_EQ(priv.user_data[0], 0xAB);
    EXPECT_EQ(priv.user_data[255], 0xCD);
}

TEST_F(ConnectTokenTest, GenerateMultipleServers) {
    uint8_t key[32];
    netudp::crypto::random_bytes(key, 32);

    const char* servers[] = {"10.0.0.1:7777", "10.0.0.2:7777", "10.0.0.3:7777"};
    uint8_t token[2048] = {};

    ASSERT_EQ(netudp_generate_connect_token(3, servers, 600, 15, 99, 0x1234, key, nullptr, token), NETUDP_OK);

    netudp_address_t srv2 = {};
    netudp_parse_address("10.0.0.2:7777", &srv2);

    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    netudp::PrivateConnectToken priv = {};
    ASSERT_EQ(netudp::validate_connect_token(token, 0x1234, key, now, &srv2, &priv), NETUDP_OK);
    EXPECT_EQ(priv.num_server_addresses, 3U);
    EXPECT_EQ(priv.client_id, 99U);
}

TEST_F(ConnectTokenTest, ExpiredTokenRejected) {
    uint8_t key[32];
    netudp::crypto::random_bytes(key, 32);

    const char* servers[] = {"127.0.0.1:7777"};
    uint8_t token[2048] = {};

    ASSERT_EQ(netudp_generate_connect_token(1, servers, 1, 10, 1, 1, key, nullptr, token), NETUDP_OK);

    /* Simulate time far in the future */
    uint64_t future = static_cast<uint64_t>(std::time(nullptr)) + 3600;
    netudp::PrivateConnectToken priv = {};
    EXPECT_EQ(netudp::validate_connect_token(token, 1, key, future, nullptr, &priv), NETUDP_ERROR_TIMEOUT);
}

TEST_F(ConnectTokenTest, WrongProtocolIdRejected) {
    uint8_t key[32];
    netudp::crypto::random_bytes(key, 32);

    const char* servers[] = {"127.0.0.1:7777"};
    uint8_t token[2048] = {};

    ASSERT_EQ(netudp_generate_connect_token(1, servers, 300, 10, 1, 0xAAAA, key, nullptr, token), NETUDP_OK);

    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    netudp::PrivateConnectToken priv = {};
    EXPECT_EQ(netudp::validate_connect_token(token, 0xBBBB, key, now, nullptr, &priv), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(ConnectTokenTest, WrongKeyRejected) {
    uint8_t key1[32], key2[32];
    netudp::crypto::random_bytes(key1, 32);
    netudp::crypto::random_bytes(key2, 32);

    const char* servers[] = {"127.0.0.1:7777"};
    uint8_t token[2048] = {};

    ASSERT_EQ(netudp_generate_connect_token(1, servers, 300, 10, 1, 1, key1, nullptr, token), NETUDP_OK);

    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    netudp::PrivateConnectToken priv = {};
    EXPECT_EQ(netudp::validate_connect_token(token, 1, key2, now, nullptr, &priv), NETUDP_ERROR_CRYPTO);
}

TEST_F(ConnectTokenTest, TamperedTokenRejected) {
    uint8_t key[32];
    netudp::crypto::random_bytes(key, 32);

    const char* servers[] = {"127.0.0.1:7777"};
    uint8_t token[2048] = {};

    ASSERT_EQ(netudp_generate_connect_token(1, servers, 300, 10, 1, 1, key, nullptr, token), NETUDP_OK);

    /* Flip a byte in the encrypted private data */
    token[100] ^= 0xFF;

    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    netudp::PrivateConnectToken priv = {};
    EXPECT_EQ(netudp::validate_connect_token(token, 1, key, now, nullptr, &priv), NETUDP_ERROR_CRYPTO);
}

TEST_F(ConnectTokenTest, WrongServerAddressRejected) {
    uint8_t key[32];
    netudp::crypto::random_bytes(key, 32);

    const char* servers[] = {"10.0.0.1:7777"};
    uint8_t token[2048] = {};

    ASSERT_EQ(netudp_generate_connect_token(1, servers, 300, 10, 1, 1, key, nullptr, token), NETUDP_OK);

    netudp_address_t wrong_addr = {};
    netudp_parse_address("10.0.0.99:7777", &wrong_addr);

    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    netudp::PrivateConnectToken priv = {};
    EXPECT_EQ(netudp::validate_connect_token(token, 1, key, now, &wrong_addr, &priv), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(ConnectTokenTest, Fingerprint) {
    uint8_t key[32];
    netudp::crypto::random_bytes(key, 32);

    const char* servers[] = {"127.0.0.1:7777"};
    uint8_t token[2048] = {};

    ASSERT_EQ(netudp_generate_connect_token(1, servers, 300, 10, 1, 1, key, nullptr, token), NETUDP_OK);

    const uint8_t* encrypted = token + netudp::TOKEN_PRIVATE_OFFSET;
    auto fp1 = netudp::compute_token_fingerprint(key, encrypted, netudp::TOKEN_PRIVATE_ENCRYPTED_SIZE);
    auto fp2 = netudp::compute_token_fingerprint(key, encrypted, netudp::TOKEN_PRIVATE_ENCRYPTED_SIZE);

    /* Same token → same fingerprint */
    EXPECT_EQ(std::memcmp(fp1.hash, fp2.hash, 8), 0);

    /* Different key → different fingerprint */
    uint8_t key2[32];
    netudp::crypto::random_bytes(key2, 32);
    auto fp3 = netudp::compute_token_fingerprint(key2, encrypted, netudp::TOKEN_PRIVATE_ENCRYPTED_SIZE);
    EXPECT_NE(std::memcmp(fp1.hash, fp3.hash, 8), 0);
}

TEST_F(ConnectTokenTest, InvalidParams) {
    uint8_t key[32] = {};
    uint8_t token[2048] = {};

    /* Zero servers */
    EXPECT_EQ(netudp_generate_connect_token(0, nullptr, 300, 10, 1, 1, key, nullptr, token), NETUDP_ERROR_INVALID_PARAM);

    /* Too many servers */
    EXPECT_EQ(netudp_generate_connect_token(33, nullptr, 300, 10, 1, 1, key, nullptr, token), NETUDP_ERROR_INVALID_PARAM);

    /* Null key */
    const char* servers[] = {"127.0.0.1:7777"};
    EXPECT_EQ(netudp_generate_connect_token(1, servers, 300, 10, 1, 1, nullptr, nullptr, token), NETUDP_ERROR_INVALID_PARAM);

    /* Null token output */
    EXPECT_EQ(netudp_generate_connect_token(1, servers, 300, 10, 1, 1, key, nullptr, nullptr), NETUDP_ERROR_INVALID_PARAM);

    /* Bad server address */
    const char* bad_servers[] = {"not_an_address"};
    EXPECT_EQ(netudp_generate_connect_token(1, bad_servers, 300, 10, 1, 1, key, nullptr, token), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(ConnectTokenTest, ValidateNullParams) {
    netudp::PrivateConnectToken priv = {};
    EXPECT_EQ(netudp::validate_connect_token(nullptr, 0, nullptr, 0, nullptr, &priv), NETUDP_ERROR_INVALID_PARAM);
}
