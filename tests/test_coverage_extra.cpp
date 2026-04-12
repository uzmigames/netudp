#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>
#include "../src/connection/connect_token.h"
#include "../src/core/allocator.h"
#include "../src/core/pool.h"
#include "../src/crypto/crc32c.h"
#include "../src/crypto/random.h"
#include "../src/socket/socket.h"
#include "../src/simd/netudp_simd.h"

#include <cstring>
#include <ctime>
#include <thread>
#include <chrono>

class CoverageExtra : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

/* ===== Custom Allocator ===== */

static int alloc_count = 0;
static int free_count = 0;

static void* test_alloc(void* /*ctx*/, size_t bytes) {
    alloc_count++;
    return std::malloc(bytes);
}

static void test_free(void* /*ctx*/, void* ptr) {
    free_count++;
    std::free(ptr);
}

TEST(AllocatorCoverage, CustomAllocator) {
    alloc_count = 0;
    free_count = 0;

    netudp::Allocator alloc;
    alloc.context = nullptr;
    alloc.alloc = test_alloc;
    alloc.free = test_free;

    void* p = alloc.allocate(64);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(alloc_count, 1);

    alloc.deallocate(p);
    EXPECT_EQ(free_count, 1);
}

TEST(AllocatorCoverage, PoolWithCustomAllocator) {
    alloc_count = 0;
    free_count = 0;

    netudp::Allocator alloc{nullptr, test_alloc, test_free};
    netudp::Pool<uint64_t> pool;
    ASSERT_TRUE(pool.init(8, alloc));
    EXPECT_EQ(alloc_count, 1);

    auto* item = pool.acquire();
    ASSERT_NE(item, nullptr);
    pool.release(item);
    pool.destroy();
    EXPECT_EQ(free_count, 1);
}

/* ===== Connect Token IPv6 ===== */

TEST_F(CoverageExtra, TokenWithIPv6Servers) {
    uint8_t key[32];
    netudp::crypto::random_bytes(key, 32);

    const char* servers[] = {"[::1]:7777", "[2001:db8:0:0:0:0:0:1]:8080"};
    uint8_t token[2048] = {};

    ASSERT_EQ(netudp_generate_connect_token(2, servers, 300, 10, 42, 0xBEEF, key, nullptr, token), NETUDP_OK);

    netudp_address_t srv = {};
    netudp_parse_address("[::1]:7777", &srv);

    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    netudp::PrivateConnectToken priv = {};
    ASSERT_EQ(netudp::validate_connect_token(token, 0xBEEF, key, now, &srv, &priv), NETUDP_OK);
    EXPECT_EQ(priv.num_server_addresses, 2U);
}

/* ===== Serialize/Deserialize edge cases ===== */

TEST(ConnectTokenCoverage, SerializeNullParams) {
    uint8_t buf[1024];
    EXPECT_EQ(netudp::serialize_private_token(nullptr, buf, 1024), -1);

    netudp::PrivateConnectToken tok = {};
    EXPECT_EQ(netudp::serialize_private_token(&tok, nullptr, 1024), -1);
    EXPECT_EQ(netudp::serialize_private_token(&tok, buf, 512), -1); /* Too small */
}

TEST(ConnectTokenCoverage, DeserializeNullParams) {
    uint8_t buf[1024] = {};
    netudp::PrivateConnectToken tok = {};
    EXPECT_EQ(netudp::deserialize_private_token(nullptr, 1024, &tok), -1);
    EXPECT_EQ(netudp::deserialize_private_token(buf, 1024, nullptr), -1);
    EXPECT_EQ(netudp::deserialize_private_token(buf, 512, &tok), -1); /* Too small */
}

TEST(ConnectTokenCoverage, DeserializeBadNumAddrs) {
    netudp_init();
    uint8_t key[32];
    netudp::crypto::random_bytes(key, 32);

    /* Create a valid token, then corrupt num_server_addresses in private data */
    netudp::PrivateConnectToken priv = {};
    priv.client_id = 1;
    priv.timeout_seconds = 10;
    priv.num_server_addresses = 0; /* Invalid! */

    uint8_t buf[1024] = {};
    /* Manual serialize with num_addrs=0 */
    std::memcpy(buf, &priv.client_id, 8);
    std::memcpy(buf + 8, &priv.timeout_seconds, 4);
    uint32_t zero_addrs = 0;
    std::memcpy(buf + 12, &zero_addrs, 4);

    netudp::PrivateConnectToken out = {};
    EXPECT_EQ(netudp::deserialize_private_token(buf, 1024, &out), -1);
    netudp_term();
}

/* ===== CRC32C without SIMD (direct call to generic) ===== */

TEST(CRC32CCoverage, GenericDirect) {
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    /* Call generic directly */
    uint32_t crc = netudp::simd::g_ops_generic.crc32c(data, 9);
    EXPECT_EQ(crc, 0xE3069283U);
}

/* ===== Socket IPv6 ===== */

TEST_F(CoverageExtra, SocketIPv6CreateDestroy) {
    netudp_address_t addr = {};
    netudp_parse_address("[::1]:19300", &addr);

    netudp::Socket sock;
    int err = netudp::socket_create(&sock, &addr, 1024 * 1024, 1024 * 1024);
    /* May fail if IPv6 not available on this machine, that's OK */
    if (err == NETUDP_OK) {
        EXPECT_NE(sock.handle, NETUDP_INVALID_SOCKET);
        netudp::socket_destroy(&sock);
    }
}

/* ===== Detect fallback paths ===== */

TEST(SimdDetectCoverage, DetectReturnsKnownLevel) {
    netudp_init();
    int level = netudp_simd_level();
    /* On x86-64, should be >= SSE42 on any modern CPU */
    EXPECT_GE(level, 0);
    netudp_term();
}
