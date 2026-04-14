/**
 * @file test_coverage_v13.cpp
 * @brief Coverage tests for v1.3.0 features: crypto modes, replication edge cases,
 *        heartbeat fields, group edges, API gaps, quantization boundaries.
 */

#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>
#include "../src/crypto/aead_dispatch.h"
#include "../src/replication/schema.h"
#include "../src/replication/entity.h"
#include "../src/replication/replicate.h"
#include "../src/connection/connection.h"
#include "../src/group/group.h"

#include <cstring>
#include <cmath>
#include <chrono>
#include <thread>

/* ======================================================================
 * Crypto mode tests
 * ====================================================================== */

class CryptoModeTest : public ::testing::Test {};

TEST_F(CryptoModeTest, NoneEncryptDecryptRoundTrip) {
    /* Save current dispatch */
    auto old_enc = netudp::crypto::g_aead_encrypt;
    auto old_dec = netudp::crypto::g_aead_decrypt;

    netudp::crypto::aead_dispatch_init(NETUDP_CRYPTO_NONE);

    uint8_t key[32] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
    uint8_t nonce[24] = {};
    uint8_t pt[] = "Hello NONE mode";
    uint8_t ct[64] = {};
    uint8_t decoded[64] = {};

    int ct_len = netudp::crypto::g_aead_encrypt(key, nonce, nullptr, 0, pt, sizeof(pt), ct);
    EXPECT_EQ(ct_len, static_cast<int>(sizeof(pt))); /* No MAC tag */
    /* NONE mode: ct == pt (plaintext copy) */
    EXPECT_EQ(std::memcmp(ct, pt, sizeof(pt)), 0);

    int pt_len = netudp::crypto::g_aead_decrypt(key, nonce, nullptr, 0, ct, ct_len, decoded);
    EXPECT_EQ(pt_len, ct_len);
    EXPECT_EQ(std::memcmp(decoded, pt, sizeof(pt)), 0);

    /* Restore */
    netudp::crypto::g_aead_encrypt = old_enc;
    netudp::crypto::g_aead_decrypt = old_dec;
}

TEST_F(CryptoModeTest, XorEncryptDecryptRoundTrip) {
    auto old_enc = netudp::crypto::g_aead_encrypt;
    auto old_dec = netudp::crypto::g_aead_decrypt;

    netudp::crypto::aead_dispatch_init(NETUDP_CRYPTO_XOR);

    uint8_t key[32];
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i + 1);
    uint8_t nonce[24] = {};
    uint8_t pt[] = "XOR obfuscation test data!";
    uint8_t ct[64] = {};
    uint8_t decoded[64] = {};

    int ct_len = netudp::crypto::g_aead_encrypt(key, nonce, nullptr, 0, pt, sizeof(pt), ct);
    EXPECT_EQ(ct_len, static_cast<int>(sizeof(pt)));

    /* ct should differ from pt (XOR with non-zero key) */
    EXPECT_NE(std::memcmp(ct, pt, sizeof(pt)), 0);

    /* Decrypt (XOR again) should recover plaintext */
    int pt_len = netudp::crypto::g_aead_decrypt(key, nonce, nullptr, 0, ct, ct_len, decoded);
    EXPECT_EQ(pt_len, ct_len);
    EXPECT_EQ(std::memcmp(decoded, pt, sizeof(pt)), 0);

    netudp::crypto::g_aead_encrypt = old_enc;
    netudp::crypto::g_aead_decrypt = old_dec;
}

TEST_F(CryptoModeTest, XorKeyRepeatsAt32Bytes) {
    auto old_enc = netudp::crypto::g_aead_encrypt;
    auto old_dec = netudp::crypto::g_aead_decrypt;

    netudp::crypto::aead_dispatch_init(NETUDP_CRYPTO_XOR);

    uint8_t key[32];
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i + 0xA0);
    uint8_t nonce[24] = {};

    /* 64-byte payload: key should repeat at byte 32 */
    uint8_t pt[64];
    std::memset(pt, 0, sizeof(pt));
    uint8_t ct[64] = {};

    netudp::crypto::g_aead_encrypt(key, nonce, nullptr, 0, pt, 64, ct);

    /* ct[i] == key[i%32] since pt is all zeros */
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(ct[i], key[i & 31]) << "Mismatch at byte " << i;
    }

    netudp::crypto::g_aead_encrypt = old_enc;
    netudp::crypto::g_aead_decrypt = old_dec;
}

/* ======================================================================
 * Quantization edge cases
 * ====================================================================== */

class QuantizationEdgeTest : public ::testing::Test {};

TEST_F(QuantizationEdgeTest, Vec3ClampAtBoundary) {
    /* X/Y range: ±511.5, Z range: ±255.5 */
    float extreme[3] = {600.0f, -600.0f, 300.0f}; /* Exceeds range */
    uint32_t packed = netudp::vec3_quantize(extreme);
    float out[3];
    netudp::vec3_dequantize(packed, out);

    /* Should be clamped, not garbage */
    EXPECT_LE(std::fabs(out[0]), 512.0f);
    EXPECT_LE(std::fabs(out[1]), 512.0f);
    EXPECT_LE(std::fabs(out[2]), 256.0f);
}

TEST_F(QuantizationEdgeTest, Vec3Zero) {
    float zero[3] = {0.0f, 0.0f, 0.0f};
    uint32_t packed = netudp::vec3_quantize(zero);
    float out[3];
    netudp::vec3_dequantize(packed, out);
    EXPECT_NEAR(out[0], 0.0f, 0.5f);
    EXPECT_NEAR(out[1], 0.0f, 0.5f);
    EXPECT_NEAR(out[2], 0.0f, 0.5f);
}

TEST_F(QuantizationEdgeTest, QuatNegatedSameRotation) {
    float q[4] = {0.1f, 0.2f, 0.3f, 0.9273f}; /* ~unit */
    float nq[4] = {-0.1f, -0.2f, -0.3f, -0.9273f};

    uint32_t p1 = netudp::quat_quantize(q);
    uint32_t p2 = netudp::quat_quantize(nq);

    float out1[4], out2[4];
    netudp::quat_dequantize(p1, out1);
    netudp::quat_dequantize(p2, out2);

    /* Both should decode to the same rotation (dot product ~= ±1) */
    float dot = out1[0]*out2[0] + out1[1]*out2[1] + out1[2]*out2[2] + out1[3]*out2[3];
    EXPECT_GT(std::fabs(dot), 0.98f);
}

TEST_F(QuantizationEdgeTest, HalfFloatOverflow) {
    /* > 65504 should become infinity in half-float */
    uint16_t h = netudp::f32_to_half(70000.0f);
    float r = netudp::half_to_f32(h);
    EXPECT_TRUE(std::isinf(r));
}

TEST_F(QuantizationEdgeTest, HalfFloatNegativeZero) {
    uint16_t h = netudp::f32_to_half(-0.0f);
    float r = netudp::half_to_f32(h);
    EXPECT_FLOAT_EQ(r, 0.0f); /* -0.0 flushes to 0 in our simple impl */
}

TEST_F(QuantizationEdgeTest, VarintTruncatedBuffer) {
    /* Encode a multi-byte varint, then try to decode from a too-short buffer */
    uint8_t buf[10];
    int enc_len = netudp::varint_encode(300, buf); /* 300 needs 2 bytes */
    ASSERT_EQ(enc_len, 2);

    uint64_t val = 0;
    int dec = netudp::varint_decode(buf, 1, &val); /* Only 1 byte available */
    EXPECT_EQ(dec, -1); /* Truncated */
}

/* ======================================================================
 * Replication condition tests
 * ====================================================================== */

class RepConditionTest : public ::testing::Test {};

TEST_F(RepConditionTest, SkipOwnerFiltering) {
    netudp::Schema s;
    s.schema_id = 0;
    s.add_prop("pos",  netudp::PropType::VEC3, NETUDP_REP_ALL, 12, 4);
    s.add_prop("anim", netudp::PropType::U8,   NETUDP_REP_SKIP_OWNER, 1, 1);

    netudp::Entity ent;
    ent.active = true;
    ent.entity_id = 10;
    ent.schema = &s;
    ent.owner_client = 5;

    float pos[3] = {1,2,3};
    ent.set_vec3(0, pos);
    ent.set_u8(1, 42);

    uint8_t wire[256];

    /* For owner (client 5): anim should be excluded (SKIP_OWNER) */
    int len_owner = netudp::replicate_serialize(ent, wire, sizeof(wire), 5, false);
    ASSERT_GT(len_owner, 0);

    /* For non-owner (client 3): anim should be included */
    ent.dirty_mask = 0b11; /* Reset dirty */
    int len_other = netudp::replicate_serialize(ent, wire, sizeof(wire), 3, false);
    ASSERT_GT(len_other, 0);
    EXPECT_GT(len_other, len_owner); /* Non-owner gets more data */
}

TEST_F(RepConditionTest, InitialOnlySentOnceOnly) {
    netudp::Schema s;
    s.schema_id = 0;
    s.add_prop("pos",   netudp::PropType::VEC3, NETUDP_REP_ALL, 12, 4);
    s.add_prop("name",  netudp::PropType::U8,   NETUDP_REP_INITIAL_ONLY, 1, 1);

    netudp::Entity ent;
    ent.active = true;
    ent.entity_id = 20;
    ent.schema = &s;
    ent.owner_client = -1;

    float pos[3] = {10,20,30};
    ent.set_vec3(0, pos);
    ent.set_u8(1, 99);

    uint8_t wire[256];

    /* Initial snapshot: both props */
    int len_init = netudp::replicate_serialize(ent, wire, sizeof(wire), 0, true);
    ASSERT_GT(len_init, 0);

    /* Non-initial: only pos (name excluded by INITIAL_ONLY) */
    ent.dirty_mask = 0b11;
    int len_delta = netudp::replicate_serialize(ent, wire, sizeof(wire), 0, false);
    ASSERT_GT(len_delta, 0);
    EXPECT_LT(len_delta, len_init);
}

/* ======================================================================
 * Entity/Schema edge cases
 * ====================================================================== */

class EntityEdgeTest : public ::testing::Test {};

TEST_F(EntityEdgeTest, WrongTypeReturnsError) {
    netudp::Schema s;
    s.schema_id = 0;
    s.add_prop("hp", netudp::PropType::F32, NETUDP_REP_ALL, 4, 4);

    netudp::Entity ent;
    ent.active = true;
    ent.entity_id = 1;
    ent.schema = &s;

    /* Try to set as U8 when it's F32 */
    EXPECT_FALSE(ent.set_u8(0, 42));
    /* Try to set with out-of-range index */
    EXPECT_FALSE(ent.set_f32(99, 1.0f));
    EXPECT_FALSE(ent.set_f32(-1, 1.0f));
    /* Correct type works */
    EXPECT_TRUE(ent.set_f32(0, 100.0f));
}

TEST_F(EntityEdgeTest, SchemaMaxProperties) {
    netudp::Schema s;
    s.schema_id = 0;

    /* Add 64 properties (the maximum) */
    for (int i = 0; i < 64; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "p%d", i);
        int idx = s.add_prop(name, netudp::PropType::U8, NETUDP_REP_ALL, 1, 1);
        EXPECT_EQ(idx, i);
    }
    EXPECT_EQ(s.prop_count, 64);

    /* 65th should fail */
    int overflow = s.add_prop("overflow", netudp::PropType::U8, NETUDP_REP_ALL, 1, 1);
    EXPECT_EQ(overflow, -1);
}

TEST_F(EntityEdgeTest, BlobProperty) {
    netudp::Schema s;
    s.schema_id = 0;
    int p = s.add_prop("name", netudp::PropType::BLOB, NETUDP_REP_ALL, 32, 32, 32);
    EXPECT_GE(p, 0);

    netudp::Entity ent;
    ent.active = true;
    ent.entity_id = 1;
    ent.schema = &s;

    uint8_t data[32] = {1,2,3,4,5};
    EXPECT_TRUE(ent.set_blob(p, data, 32));

    /* Over capacity */
    uint8_t big[64] = {};
    EXPECT_FALSE(ent.set_blob(p, big, 64));
}

/* ======================================================================
 * Connection heartbeat fields reset
 * ====================================================================== */

class HeartbeatFieldsTest : public ::testing::Test {};

TEST_F(HeartbeatFieldsTest, ResetClearsHeartbeatFields) {
    netudp::Connection conn;
    conn.last_ping_time = 100.0;
    conn.last_pong_time = 99.0;
    conn.timeout_left = 5.0;
    conn.integrity_timeout = 10.0;
    conn.integrity_check_index = 42;
    conn.integrity_pending = true;

    conn.reset();

    EXPECT_DOUBLE_EQ(conn.last_ping_time, 0.0);
    EXPECT_DOUBLE_EQ(conn.last_pong_time, 0.0);
    EXPECT_DOUBLE_EQ(conn.timeout_left, 30.0);
    EXPECT_DOUBLE_EQ(conn.integrity_timeout, 120.0);
    EXPECT_EQ(conn.integrity_check_index, 0);
    EXPECT_FALSE(conn.integrity_pending);
}

/* ======================================================================
 * Group edge cases
 * ====================================================================== */

class GroupEdgeTest : public ::testing::Test {};

TEST_F(GroupEdgeTest, GroupClear) {
    int members[8], slots[16];
    netudp::Group g;
    g.init(0, 16, members, slots, 8);

    g.add(0);
    g.add(5);
    g.add(10);
    EXPECT_EQ(g.member_count, 3);

    g.clear();
    EXPECT_EQ(g.member_count, 0);
    EXPECT_FALSE(g.has(0));
    EXPECT_FALSE(g.has(5));
    EXPECT_FALSE(g.has(10));
}

TEST_F(GroupEdgeTest, GroupAtCapacity) {
    int members[4], slots[16];
    netudp::Group g;
    g.init(0, 16, members, slots, 4); /* Capacity 4 */

    EXPECT_TRUE(g.add(0));
    EXPECT_TRUE(g.add(1));
    EXPECT_TRUE(g.add(2));
    EXPECT_TRUE(g.add(3));
    EXPECT_EQ(g.member_count, 4);

    /* 5th should fail */
    EXPECT_FALSE(g.add(4));
}

/* ======================================================================
 * Untested public API functions
 * ====================================================================== */

class ApiCoverageTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

TEST_F(ApiCoverageTest, ServerMaxClients) {
    netudp_server_config_t cfg = {};
    cfg.protocol_id = 0x1234;
    netudp_server_t* s = netudp_server_create("127.0.0.1:29950", &cfg, 0.0);
    ASSERT_NE(s, nullptr);
    netudp_server_start(s, 64);

    EXPECT_EQ(netudp_server_max_clients(s), 64);

    netudp_server_stop(s);
    netudp_server_destroy(s);
}

TEST_F(ApiCoverageTest, ServerNumIoThreads) {
    netudp_server_config_t cfg = {};
    cfg.protocol_id = 0x1234;
    netudp_server_t* s = netudp_server_create("127.0.0.1:29951", &cfg, 0.0);
    ASSERT_NE(s, nullptr);
    netudp_server_start(s, 8);

    int threads = netudp_server_num_io_threads(s);
    EXPECT_GE(threads, 1);

    netudp_server_stop(s);
    netudp_server_destroy(s);
}

TEST_F(ApiCoverageTest, ServerGetStats) {
    netudp_server_config_t cfg = {};
    cfg.protocol_id = 0x5678;
    netudp_server_t* s = netudp_server_create("127.0.0.1:29952", &cfg, 0.0);
    ASSERT_NE(s, nullptr);
    netudp_server_start(s, 16);

    netudp_server_stats_t stats = {};
    netudp_server_get_stats(s, &stats);
    EXPECT_EQ(stats.connected_clients, 0);
    EXPECT_EQ(stats.max_clients, 16);

    netudp_server_stop(s);
    netudp_server_destroy(s);
}

TEST_F(ApiCoverageTest, WindowsWfpActive) {
    int result = netudp_windows_is_wfp_active();
    /* Should return 0 or 1, not crash */
    EXPECT_GE(result, -1);
    EXPECT_LE(result, 1);
}

TEST_F(ApiCoverageTest, ClientIndexBeforeConnect) {
    netudp_client_config_t cfg = {};
    cfg.protocol_id = 0x1234;
    netudp_client_t* c = netudp_client_create(nullptr, &cfg, 0.0);
    ASSERT_NE(c, nullptr);

    /* Before connecting, index should be -1 */
    EXPECT_EQ(netudp_client_index(c), -1);

    netudp_client_destroy(c);
}

TEST_F(ApiCoverageTest, ClientIndexNull) {
    EXPECT_EQ(netudp_client_index(nullptr), -1);
}

TEST_F(ApiCoverageTest, SchemaEntityLimits) {
    netudp_server_config_t cfg = {};
    cfg.protocol_id = 0x9999;
    netudp_server_t* s = netudp_server_create("127.0.0.1:29953", &cfg, 0.0);
    ASSERT_NE(s, nullptr);
    netudp_server_start(s, 4);

    /* Create max schemas (64) */
    int last_sid = -1;
    for (int i = 0; i < 64; ++i) {
        int sid = netudp_schema_create(s);
        if (sid >= 0) { last_sid = sid; }
    }
    EXPECT_GE(last_sid, 0);

    /* 65th should fail */
    int overflow_sid = netudp_schema_create(s);
    EXPECT_EQ(overflow_sid, -1);

    /* Create entity on valid schema */
    int sid = 0;
    netudp_schema_add_u8(s, sid, "test", NETUDP_REP_ALL);
    uint16_t eid = netudp_entity_create(s, sid);
    EXPECT_GT(eid, static_cast<uint16_t>(0));

    /* Entity on invalid schema */
    uint16_t bad = netudp_entity_create(s, 999);
    EXPECT_EQ(bad, static_cast<uint16_t>(0));

    netudp_entity_destroy(s, eid);
    netudp_server_stop(s);
    netudp_server_destroy(s);
}
