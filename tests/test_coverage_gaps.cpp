#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_buffer.h>
#include <netudp/netudp_token.h>
#include "../src/crypto/aead.h"
#include "../src/crypto/xchacha.h"
#include "../src/crypto/packet_crypto.h"
#include "../src/crypto/random.h"
#include "../src/core/pool.h"
#include "../src/core/ring_buffer.h"
#include "../src/core/hash_map.h"
#include "../src/core/address.h"
#include "../src/socket/socket.h"

#include "../src/crypto/crc32c.h"

#include <cstring>
#include <thread>
#include <chrono>

/* =====================================================================
 * API STUBS — verify all stubs return correct error codes
 * ===================================================================== */

TEST(ApiStubs, ServerCreateReturnsNull) {
    netudp_init();
    EXPECT_EQ(netudp_server_create("0.0.0.0:7777", nullptr, 0.0), nullptr);
    netudp_term();
}

TEST(ApiStubs, ClientCreateReturnsNull) {
    netudp_init();
    EXPECT_EQ(netudp_client_create("0.0.0.0:7777", nullptr, 0.0), nullptr);
    netudp_term();
}

TEST(ApiStubs, ServerSendReturnsNotInitialized) {
    netudp_init();
    EXPECT_EQ(netudp_server_send(nullptr, 0, 0, "x", 1, 0), NETUDP_ERROR_NOT_INITIALIZED);
    netudp_term();
}

TEST(ApiStubs, ClientSendReturnsNotInitialized) {
    netudp_init();
    EXPECT_EQ(netudp_client_send(nullptr, 0, "x", 1, 0), NETUDP_ERROR_NOT_INITIALIZED);
    netudp_term();
}

TEST(ApiStubs, ServerReceiveReturnsZero) {
    netudp_init();
    netudp_message_t* msgs[1];
    EXPECT_EQ(netudp_server_receive(nullptr, 0, msgs, 1), 0);
    netudp_term();
}

TEST(ApiStubs, ClientReceiveReturnsZero) {
    netudp_init();
    netudp_message_t* msgs[1];
    EXPECT_EQ(netudp_client_receive(nullptr, msgs, 1), 0);
    netudp_term();
}

TEST(ApiStubs, ClientStateReturnsDisconnected) {
    netudp_init();
    EXPECT_EQ(netudp_client_state(nullptr), 0);
    netudp_term();
}

TEST(ApiStubs, GenerateTokenReturnsNotInitialized) {
    netudp_init();
    uint8_t key[32] = {};
    uint8_t ud[256] = {};
    uint8_t token[2048] = {};
    const char* servers[] = {"127.0.0.1:7777"};
    EXPECT_EQ(netudp_generate_connect_token(1, servers, 300, 10, 1, 1, key, ud, token),
              NETUDP_ERROR_NOT_INITIALIZED);
    netudp_term();
}

TEST(ApiStubs, BufferAcquireReturnsNull) {
    netudp_init();
    EXPECT_EQ(netudp_server_acquire_buffer(nullptr), nullptr);
    netudp_term();
}

TEST(ApiStubs, BufferSendReturnsNotInitialized) {
    netudp_init();
    EXPECT_EQ(netudp_server_send_buffer(nullptr, 0, 0, nullptr, 0), NETUDP_ERROR_NOT_INITIALIZED);
    netudp_term();
}

TEST(ApiStubs, MessageReleaseNoOp) {
    netudp_init();
    netudp_message_release(nullptr); /* Should not crash */
    netudp_term();
}

TEST(ApiStubs, StubCallsNoOp) {
    netudp_init();
    netudp_server_start(nullptr, 0);
    netudp_server_stop(nullptr);
    netudp_server_update(nullptr, 0.0);
    netudp_server_destroy(nullptr);
    netudp_client_connect(nullptr, nullptr);
    netudp_client_update(nullptr, 0.0);
    netudp_client_disconnect(nullptr);
    netudp_client_destroy(nullptr);
    netudp_server_broadcast(nullptr, 0, nullptr, 0, 0);
    netudp_server_broadcast_except(nullptr, 0, 0, nullptr, 0, 0);
    netudp_server_flush(nullptr, 0);
    netudp_client_flush(nullptr);
    netudp_term();
}

TEST(ApiStubs, BufferWriteReadNoOp) {
    netudp_init();
    netudp_buffer_write_u8(nullptr, 0);
    netudp_buffer_write_u16(nullptr, 0);
    netudp_buffer_write_u32(nullptr, 0);
    netudp_buffer_write_u64(nullptr, 0);
    netudp_buffer_write_f32(nullptr, 0.0F);
    netudp_buffer_write_f64(nullptr, 0.0);
    netudp_buffer_write_varint(nullptr, 0);
    netudp_buffer_write_bytes(nullptr, nullptr, 0);
    netudp_buffer_write_string(nullptr, nullptr, 0);
    EXPECT_EQ(netudp_buffer_read_u8(nullptr), 0);
    EXPECT_EQ(netudp_buffer_read_u16(nullptr), 0);
    EXPECT_EQ(netudp_buffer_read_u32(nullptr), 0);
    EXPECT_EQ(netudp_buffer_read_u64(nullptr), 0U);
    EXPECT_EQ(netudp_buffer_read_f32(nullptr), 0.0F);
    EXPECT_EQ(netudp_buffer_read_f64(nullptr), 0.0);
    EXPECT_EQ(netudp_buffer_read_varint(nullptr), 0);
    netudp_term();
}

/* =====================================================================
 * AEAD — edge cases
 * ===================================================================== */

TEST(AeadEdge, EncryptNegativePtLen) {
    uint8_t key[32] = {};
    uint8_t nonce[24] = {};
    uint8_t ct[16] = {};
    EXPECT_EQ(netudp::crypto::aead_encrypt(key, nonce, nullptr, 0, nullptr, -1, ct), -1);
}

TEST(AeadEdge, DecryptTooShort) {
    uint8_t key[32] = {};
    uint8_t nonce[24] = {};
    uint8_t ct[8] = {};
    uint8_t pt[8] = {};
    EXPECT_EQ(netudp::crypto::aead_decrypt(key, nonce, nullptr, 0, ct, 8, pt), -1);
    EXPECT_EQ(netudp::crypto::aead_decrypt(key, nonce, nullptr, 0, ct, 15, pt), -1);
}

TEST(AeadEdge, EncryptEmptyPayload) {
    uint8_t key[32] = {1};
    uint8_t nonce[24] = {2};
    uint8_t ct[16] = {};
    int ct_len = netudp::crypto::aead_encrypt(key, nonce, nullptr, 0, nullptr, 0, ct);
    EXPECT_EQ(ct_len, 16); /* Just the MAC */

    uint8_t pt[1] = {};
    int pt_len = netudp::crypto::aead_decrypt(key, nonce, nullptr, 0, ct, 16, pt);
    EXPECT_EQ(pt_len, 0);
}

/* =====================================================================
 * XChaCha — edge cases
 * ===================================================================== */

TEST(XChachaEdge, EncryptNegativePtLen) {
    uint8_t key[32] = {};
    uint8_t nonce[24] = {};
    uint8_t ct[16] = {};
    EXPECT_EQ(netudp::crypto::xchacha_encrypt(key, nonce, nullptr, 0, nullptr, -1, ct), -1);
}

TEST(XChachaEdge, DecryptTooShort) {
    uint8_t key[32] = {};
    uint8_t nonce[24] = {};
    uint8_t ct[8] = {};
    uint8_t pt[8] = {};
    EXPECT_EQ(netudp::crypto::xchacha_decrypt(key, nonce, nullptr, 0, ct, 15, pt), -1);
}

/* =====================================================================
 * Packet Crypto — null/invalid param paths
 * ===================================================================== */

class PacketCryptoEdge : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

TEST_F(PacketCryptoEdge, EncryptNullEpoch) {
    uint8_t pt[4] = {1, 2, 3, 4};
    uint8_t ct[32] = {};
    EXPECT_EQ(netudp::crypto::packet_encrypt(nullptr, 0, 0, pt, 4, ct), -1);
}

TEST_F(PacketCryptoEdge, EncryptNullPt) {
    netudp::crypto::KeyEpoch epoch;
    uint8_t ct[32] = {};
    EXPECT_EQ(netudp::crypto::packet_encrypt(&epoch, 0, 0, nullptr, 4, ct), -1);
}

TEST_F(PacketCryptoEdge, EncryptNullCt) {
    netudp::crypto::KeyEpoch epoch;
    uint8_t pt[4] = {};
    EXPECT_EQ(netudp::crypto::packet_encrypt(&epoch, 0, 0, pt, 4, nullptr), -1);
}

TEST_F(PacketCryptoEdge, EncryptNegativeLen) {
    netudp::crypto::KeyEpoch epoch;
    uint8_t pt[4] = {};
    uint8_t ct[32] = {};
    EXPECT_EQ(netudp::crypto::packet_encrypt(&epoch, 0, 0, pt, -1, ct), -1);
}

TEST_F(PacketCryptoEdge, DecryptNullEpoch) {
    uint8_t ct[32] = {};
    uint8_t pt[32] = {};
    EXPECT_EQ(netudp::crypto::packet_decrypt(nullptr, 0, 0, 0, ct, 32, pt), -1);
}

TEST_F(PacketCryptoEdge, DecryptTooShortCt) {
    netudp::crypto::KeyEpoch epoch;
    uint8_t ct[8] = {};
    uint8_t pt[8] = {};
    EXPECT_EQ(netudp::crypto::packet_decrypt(&epoch, 0, 0, 0, ct, 8, pt), -1);
}

TEST_F(PacketCryptoEdge, DecryptOldNonceRejected) {
    netudp::crypto::KeyEpoch epoch;
    netudp::crypto::random_bytes(epoch.tx_key, 32);
    std::memcpy(epoch.rx_key, epoch.tx_key, 32);

    /* Advance replay window far ahead */
    for (uint64_t i = 0; i < 300; ++i) {
        epoch.replay.advance(i);
    }
    /* Nonce 0 is now too old (0 + 256 <= 299) */
    uint8_t ct[32] = {};
    uint8_t pt[32] = {};
    EXPECT_EQ(netudp::crypto::packet_decrypt(&epoch, 0, 0, 0, ct, 32, pt), -1);
}

/* =====================================================================
 * Address — edge cases
 * ===================================================================== */

class AddressEdge : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

TEST_F(AddressEdge, ParsePortZero) {
    netudp_address_t addr = {};
    EXPECT_EQ(netudp_parse_address("127.0.0.1:0", &addr), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(AddressEdge, ParsePortTooHigh) {
    netudp_address_t addr = {};
    EXPECT_EQ(netudp_parse_address("127.0.0.1:99999", &addr), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(AddressEdge, ParseIPv6DoubleDoubleColon) {
    netudp_address_t addr = {};
    EXPECT_EQ(netudp_parse_address("[::1::2]:7777", &addr), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(AddressEdge, ParseIPv6MissingCloseBracket) {
    netudp_address_t addr = {};
    EXPECT_EQ(netudp_parse_address("[::1:7777", &addr), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(AddressEdge, ParseIPv6NoPortAfterBracket) {
    netudp_address_t addr = {};
    EXPECT_EQ(netudp_parse_address("[::1]7777", &addr), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(AddressEdge, ParseIPv6PortZero) {
    netudp_address_t addr = {};
    EXPECT_EQ(netudp_parse_address("[::1]:0", &addr), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(AddressEdge, ParseEmptyString) {
    netudp_address_t addr = {};
    EXPECT_EQ(netudp_parse_address("", &addr), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(AddressEdge, ParseJustColon) {
    netudp_address_t addr = {};
    EXPECT_EQ(netudp_parse_address(":7777", &addr), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(AddressEdge, ToStringNull) {
    char buf[64] = {};
    EXPECT_EQ(netudp_address_to_string(nullptr, buf, 64), buf);
}

TEST_F(AddressEdge, ToStringSmallBuffer) {
    char buf[2] = {};
    netudp_address_t addr = {};
    EXPECT_EQ(netudp_address_to_string(&addr, buf, 1), buf); /* buf_len < 2 */
}

TEST_F(AddressEdge, ToStringNoneType) {
    netudp_address_t addr = {};
    addr.type = NETUDP_ADDRESS_NONE;
    char buf[64] = {};
    netudp_address_to_string(&addr, buf, 64);
    EXPECT_STREQ(buf, "none:0");
}

TEST_F(AddressEdge, EqualBothNull) {
    EXPECT_EQ(netudp_address_equal(nullptr, nullptr), 0);
}

TEST_F(AddressEdge, EqualOneNull) {
    netudp_address_t addr = {};
    EXPECT_EQ(netudp_address_equal(&addr, nullptr), 0);
    EXPECT_EQ(netudp_address_equal(nullptr, &addr), 0);
}

TEST_F(AddressEdge, EqualNoneType) {
    netudp_address_t a = netudp::address_zero();
    netudp_address_t b = netudp::address_zero();
    a.type = NETUDP_ADDRESS_NONE;
    b.type = NETUDP_ADDRESS_NONE;
    EXPECT_EQ(netudp_address_equal(&a, &b), 1);
}

TEST_F(AddressEdge, EqualNoSimd) {
    /* Test the fallback memcmp path — address_equal checks g_simd != nullptr */
    netudp_address_t a = {}, b = {};
    netudp_parse_address("10.0.0.1:5000", &a);
    netudp_parse_address("10.0.0.1:5000", &b);
    EXPECT_EQ(netudp_address_equal(&a, &b), 1);
}

/* =====================================================================
 * Pool — edge cases
 * ===================================================================== */

struct SmallItem {
    uint64_t val;
};

TEST(PoolEdge, InitZeroCapacity) {
    netudp::Pool<SmallItem> pool;
    EXPECT_FALSE(pool.init(0));
}

TEST(PoolEdge, InitNegativeCapacity) {
    netudp::Pool<SmallItem> pool;
    EXPECT_FALSE(pool.init(-1));
}

TEST(PoolEdge, DoubleInit) {
    netudp::Pool<SmallItem> pool;
    EXPECT_TRUE(pool.init(4));
    EXPECT_FALSE(pool.init(8)); /* Already initialized */
}

TEST(PoolEdge, ReleaseNull) {
    netudp::Pool<SmallItem> pool;
    pool.init(2);
    pool.release(nullptr); /* Should not crash, no effect */
    EXPECT_EQ(pool.available(), 2);
}

TEST(PoolEdge, MoveConstructor) {
    netudp::Pool<SmallItem> a;
    a.init(4);
    a.acquire();
    EXPECT_EQ(a.in_use(), 1);

    netudp::Pool<SmallItem> b(std::move(a));
    EXPECT_EQ(b.in_use(), 1);
    EXPECT_EQ(b.capacity(), 4);
    EXPECT_EQ(a.capacity(), 0);
}

TEST(PoolEdge, MoveAssignment) {
    netudp::Pool<SmallItem> a;
    a.init(4);
    a.acquire();

    netudp::Pool<SmallItem> b;
    b = std::move(a);
    EXPECT_EQ(b.in_use(), 1);
    EXPECT_EQ(a.capacity(), 0);
}

/* =====================================================================
 * RingBuffer — edge cases
 * ===================================================================== */

TEST(RingBufferEdge, PopFromEmpty) {
    netudp::FixedRingBuffer<int, 4> rb;
    int val = 42;
    EXPECT_FALSE(rb.pop_front(&val));
    EXPECT_EQ(val, 42); /* Unchanged */
}

TEST(RingBufferEdge, PopWithNullOut) {
    netudp::FixedRingBuffer<int, 4> rb;
    rb.push_back(10);
    EXPECT_TRUE(rb.pop_front(nullptr)); /* Discards the value */
    EXPECT_TRUE(rb.is_empty());
}

TEST(RingBufferEdge, AtSeqAccess) {
    netudp::FixedRingBuffer<int, 8> rb;
    rb.at_seq(3) = 42;
    EXPECT_EQ(rb.at_seq(3), 42);
    /* Wrap around */
    rb.at_seq(11) = 99; /* 11 & 7 = 3, overwrites */
    EXPECT_EQ(rb.at_seq(3), 99);
}

TEST(RingBufferEdge, ClearAlreadyEmpty) {
    netudp::FixedRingBuffer<int, 4> rb;
    rb.clear();
    EXPECT_TRUE(rb.is_empty());
}

/* =====================================================================
 * HashMap — edge cases
 * ===================================================================== */

struct TinyKey { uint32_t id; };

TEST(HashMapEdge, RemoveNonExistent) {
    netudp::FixedHashMap<TinyKey, int, 8> map;
    TinyKey k{42};
    EXPECT_FALSE(map.remove(k));
}

TEST(HashMapEdge, ClearAndReuse) {
    netudp::FixedHashMap<TinyKey, int, 8> map;
    map.insert(TinyKey{1}, 10);
    map.insert(TinyKey{2}, 20);
    map.clear();
    EXPECT_TRUE(map.is_empty());
    EXPECT_EQ(map.size(), 0);

    /* Reuse after clear */
    map.insert(TinyKey{3}, 30);
    EXPECT_EQ(*map.find(TinyKey{3}), 30);
}

TEST(HashMapEdge, ForEachStopsEarly) {
    netudp::FixedHashMap<TinyKey, int, 8> map;
    map.insert(TinyKey{1}, 10);
    map.insert(TinyKey{2}, 20);
    map.insert(TinyKey{3}, 30);

    int count = 0;
    map.for_each([&count](const TinyKey& /*key*/, int& /*value*/) -> bool {
        count++;
        return false; /* Stop after first */
    });
    EXPECT_EQ(count, 1);
}

TEST(HashMapEdge, ConstFind) {
    netudp::FixedHashMap<TinyKey, int, 8> map;
    map.insert(TinyKey{1}, 42);

    const auto& cmap = map;
    const int* v = cmap.find(TinyKey{1});
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(*v, 42);

    EXPECT_EQ(cmap.find(TinyKey{99}), nullptr);
}

/* =====================================================================
 * Socket — edge cases
 * ===================================================================== */

class SocketEdge : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

TEST_F(SocketEdge, CreateNullParams) {
    EXPECT_EQ(netudp::socket_create(nullptr, nullptr, 0, 0), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(SocketEdge, SendNullSocket) {
    netudp_address_t dest = {};
    EXPECT_EQ(netudp::socket_send(nullptr, &dest, "x", 1), -1);
}

TEST_F(SocketEdge, SendInvalidHandle) {
    netudp::Socket sock;
    netudp_address_t dest = {};
    EXPECT_EQ(netudp::socket_send(&sock, &dest, "x", 1), -1);
}

TEST_F(SocketEdge, RecvNullSocket) {
    netudp_address_t from = {};
    char buf[64] = {};
    EXPECT_EQ(netudp::socket_recv(nullptr, &from, buf, 64), -1);
}

TEST_F(SocketEdge, RecvInvalidHandle) {
    netudp::Socket sock;
    netudp_address_t from = {};
    char buf[64] = {};
    EXPECT_EQ(netudp::socket_recv(&sock, &from, buf, 64), -1);
}

TEST_F(SocketEdge, DestroyNull) {
    netudp::socket_destroy(nullptr); /* Should not crash */
}

TEST_F(SocketEdge, DestroyInvalidHandle) {
    netudp::Socket sock;
    netudp::socket_destroy(&sock); /* Already invalid, no-op */
    EXPECT_EQ(sock.handle, NETUDP_INVALID_SOCKET);
}

TEST_F(SocketEdge, RecvNullFrom) {
    netudp_address_t addr = {};
    netudp_parse_address("127.0.0.1:19200", &addr);

    netudp::Socket sock;
    ASSERT_EQ(netudp::socket_create(&sock, &addr, 1024 * 1024, 1024 * 1024), NETUDP_OK);

    /* Send to self */
    netudp::socket_send(&sock, &addr, "test", 4);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    char buf[64] = {};
    int n = netudp::socket_recv(&sock, nullptr, buf, 64); /* from = null */
    EXPECT_GT(n, 0);
    EXPECT_EQ(std::memcmp(buf, "test", 4), 0);

    netudp::socket_destroy(&sock);
}

/* =====================================================================
 * CRC32C — edge cases
 * ===================================================================== */

TEST(CRC32CEdge, EmptyInput) {
    netudp_init();
    EXPECT_EQ(netudp::crypto::crc32c(nullptr, 0), 0x00000000U);
    netudp_term();
}

TEST(CRC32CEdge, SingleByte) {
    netudp_init();
    uint8_t byte = 0x00;
    uint32_t crc = netudp::crypto::crc32c(&byte, 1);
    EXPECT_NE(crc, 0U); /* Non-trivial hash */
    netudp_term();
}

/* =====================================================================
 * Random — edge cases
 * ===================================================================== */

TEST(RandomEdge, NullPointer) {
    netudp::crypto::random_bytes(nullptr, 10); /* Should not crash */
}

TEST(RandomEdge, ZeroLength) {
    uint8_t buf[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    netudp::crypto::random_bytes(buf, 0); /* No-op */
    EXPECT_EQ(buf[0], 0xAA);
}

TEST(RandomEdge, NegativeLength) {
    uint8_t buf[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    netudp::crypto::random_bytes(buf, -1); /* No-op */
    EXPECT_EQ(buf[0], 0xAA);
}
