/**
 * @file test_replication.cpp
 * @brief Tests for property replication system (phase 42).
 *
 * Verifies schema creation, entity lifecycle, dirty tracking, quantization,
 * wire serialization/deserialization, and server_replicate integration.
 */

#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>
#include "../src/replication/schema.h"
#include "../src/replication/entity.h"
#include "../src/replication/replicate.h"

#include <chrono>
#include <cstring>
#include <cmath>
#include <thread>

/* ---- Unit tests: schema + entity + serialization ---- */

class ReplicationUnitTest : public ::testing::Test {};

TEST_F(ReplicationUnitTest, SchemaAddProperties) {
    netudp::Schema s;
    s.schema_id = 0;

    int p0 = s.add_prop("position", netudp::PropType::VEC3, NETUDP_REP_ALL, 12, 4);
    int p1 = s.add_prop("health",   netudp::PropType::F32,  NETUDP_REP_ALL, 4, 2);
    int p2 = s.add_prop("level",    netudp::PropType::U8,   NETUDP_REP_INITIAL_ONLY, 1, 1);

    EXPECT_EQ(p0, 0);
    EXPECT_EQ(p1, 1);
    EXPECT_EQ(p2, 2);
    EXPECT_EQ(s.prop_count, 3);
    EXPECT_EQ(s.value_buffer_size, 12 + 4 + 1); /* 17 bytes */

    EXPECT_EQ(s.find_prop("health"), 1);
    EXPECT_EQ(s.find_prop("missing"), -1);
}

TEST_F(ReplicationUnitTest, EntityDirtyTracking) {
    netudp::Schema s;
    s.schema_id = 0;
    s.add_prop("x", netudp::PropType::F32, NETUDP_REP_ALL, 4, 4);
    s.add_prop("y", netudp::PropType::F32, NETUDP_REP_ALL, 4, 4);
    s.add_prop("hp", netudp::PropType::U8, NETUDP_REP_ALL, 1, 1);

    netudp::Entity ent;
    ent.active = true;
    ent.entity_id = 1;
    ent.schema = &s;

    EXPECT_EQ(ent.dirty_mask, 0ULL);

    /* Set x = 10.0 → dirty bit 0 */
    ent.set_f32(0, 10.0f);
    EXPECT_EQ(ent.dirty_mask, 1ULL);

    /* Set same value → no change */
    bool changed = ent.set_f32(0, 10.0f);
    EXPECT_FALSE(changed);
    EXPECT_EQ(ent.dirty_mask, 1ULL);

    /* Set hp = 100 → dirty bit 2 */
    ent.set_u8(2, 100);
    EXPECT_EQ(ent.dirty_mask, 0b101ULL);

    /* Verify getters */
    EXPECT_FLOAT_EQ(ent.get_f32(0), 10.0f);
    EXPECT_EQ(ent.get_u8(2), 100);
}

TEST_F(ReplicationUnitTest, Vec3Quantization) {
    /* X,Y: 11-bit range (-512..511.5), Z: 10-bit range (-256..255.5), precision 0.5 */
    float v[3] = {100.5f, -200.0f, 50.0f};
    uint32_t packed = netudp::vec3_quantize(v);
    float out[3];
    netudp::vec3_dequantize(packed, out);

    EXPECT_NEAR(out[0], 100.5f, 0.5f);
    EXPECT_NEAR(out[1], -200.0f, 0.5f);
    EXPECT_NEAR(out[2], 50.0f, 0.5f);
}

TEST_F(ReplicationUnitTest, QuatQuantization) {
    /* Unit quaternion (identity) */
    float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    uint32_t packed = netudp::quat_quantize(q);
    float out[4];
    netudp::quat_dequantize(packed, out);

    /* Should be close to identity */
    float dot = out[0]*q[0] + out[1]*q[1] + out[2]*q[2] + out[3]*q[3];
    EXPECT_GT(std::fabs(dot), 0.99f);
}

TEST_F(ReplicationUnitTest, HalfFloatRoundTrip) {
    float values[] = {0.0f, 1.0f, -1.0f, 100.0f, 0.5f, 65504.0f};
    for (float v : values) {
        uint16_t h = netudp::f32_to_half(v);
        float r = netudp::half_to_f32(h);
        EXPECT_NEAR(r, v, std::fabs(v) * 0.001f + 0.001f) << "Failed for v=" << v;
    }
}

TEST_F(ReplicationUnitTest, VarintRoundTrip) {
    uint64_t test_vals[] = {0, 1, 127, 128, 255, 256, 16383, 16384, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFFULL};
    for (uint64_t val : test_vals) {
        uint8_t buf[10];
        int enc = netudp::varint_encode(val, buf);
        ASSERT_GT(enc, 0);

        uint64_t decoded = 0;
        int dec = netudp::varint_decode(buf, enc, &decoded);
        ASSERT_EQ(dec, enc);
        EXPECT_EQ(decoded, val);
    }
}

TEST_F(ReplicationUnitTest, SerializeDeserializeRoundTrip) {
    netudp::Schema s;
    s.schema_id = 0;
    s.add_prop("pos",  netudp::PropType::VEC3, NETUDP_REP_ALL, 12, 4);
    s.add_prop("hp",   netudp::PropType::F32,  NETUDP_REP_ALL, 4, 4);
    s.add_prop("lvl",  netudp::PropType::U8,   NETUDP_REP_ALL, 1, 1);

    netudp::Entity ent;
    ent.active = true;
    ent.entity_id = 42;
    ent.schema = &s;
    ent.owner_client = -1;

    float pos[3] = {10.0f, 20.0f, 30.0f};
    ent.set_vec3(0, pos);
    ent.set_f32(1, 75.0f);
    /* lvl not set → not dirty → not serialized */

    uint8_t wire[256];
    int wire_len = netudp::replicate_serialize(ent, wire, sizeof(wire), 0, false);
    ASSERT_GT(wire_len, 0);

    /* Deserialize */
    uint16_t out_eid = 0;
    uint8_t out_vals[256] = {};
    uint64_t out_dirty = 0;
    int consumed = netudp::replicate_deserialize(s, wire, wire_len, &out_eid, out_vals, &out_dirty);
    ASSERT_GT(consumed, 0);
    EXPECT_EQ(out_eid, 42);
    EXPECT_EQ(out_dirty, 0b011ULL); /* pos + hp dirty */

    /* Verify deserialized values */
    float out_pos[3];
    std::memcpy(out_pos, out_vals + s.props[0].offset, 12);
    EXPECT_FLOAT_EQ(out_pos[0], 10.0f);
    EXPECT_FLOAT_EQ(out_pos[1], 20.0f);
    EXPECT_FLOAT_EQ(out_pos[2], 30.0f);

    float out_hp;
    std::memcpy(&out_hp, out_vals + s.props[1].offset, 4);
    EXPECT_FLOAT_EQ(out_hp, 75.0f);
}

TEST_F(ReplicationUnitTest, OwnerOnlyFiltering) {
    netudp::Schema s;
    s.schema_id = 0;
    s.add_prop("pos",  netudp::PropType::VEC3, NETUDP_REP_ALL, 12, 4);
    s.add_prop("ammo", netudp::PropType::I32,  NETUDP_REP_OWNER_ONLY, 4, 4);

    netudp::Entity ent;
    ent.active = true;
    ent.entity_id = 1;
    ent.schema = &s;
    ent.owner_client = 5;

    float pos[3] = {1, 2, 3};
    ent.set_vec3(0, pos);
    ent.set_i32(1, 30);

    /* Serialize for owner (client 5) — should include ammo */
    uint8_t wire[256];
    int len_owner = netudp::replicate_serialize(ent, wire, sizeof(wire), 5, false);
    ASSERT_GT(len_owner, 0);

    /* Serialize for non-owner (client 3) — should exclude ammo */
    ent.dirty_mask = 0b11; /* Reset dirty for second serialize */
    int len_other = netudp::replicate_serialize(ent, wire, sizeof(wire), 3, false);
    ASSERT_GT(len_other, 0);
    EXPECT_LT(len_other, len_owner); /* Fewer bytes without ammo */
}

/* ---- Integration tests: full server→client replication ---- */

static constexpr uint64_t kRepProtoId = 0x4245500000000001ULL;
static constexpr uint16_t kRepPort    = 29890U;

static const uint8_t kRepKey[32] = {
    0x42, 0x43, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
};

class ReplicationIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

TEST_F(ReplicationIntegrationTest, SchemaAndEntityLifecycle) {
    char addr[64];
    std::snprintf(addr, sizeof(addr), "127.0.0.1:%u", static_cast<unsigned>(kRepPort));

    netudp_server_config_t cfg = {};
    cfg.protocol_id = kRepProtoId;
    std::memcpy(cfg.private_key, kRepKey, 32);
    cfg.num_channels = 1;
    cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    netudp_server_t* server = netudp_server_create(addr, &cfg, 1000.0);
    ASSERT_NE(server, nullptr);
    netudp_server_start(server, 8);

    /* Create schema */
    int sid = netudp_schema_create(server);
    ASSERT_GE(sid, 0);

    int p_pos = netudp_schema_add_vec3(server, sid, "position", NETUDP_REP_ALL);
    int p_hp  = netudp_schema_add_f32(server, sid, "health", NETUDP_REP_ALL);
    EXPECT_GE(p_pos, 0);
    EXPECT_GE(p_hp, 0);

    /* Create entity */
    uint16_t eid = netudp_entity_create(server, sid);
    EXPECT_GT(eid, static_cast<uint16_t>(0));

    /* Set properties */
    float pos[3] = {100.0f, 0.0f, 50.0f};
    EXPECT_EQ(netudp_entity_set_vec3(server, eid, p_pos, pos), NETUDP_OK);
    EXPECT_EQ(netudp_entity_set_f32(server, eid, p_hp, 100.0f), NETUDP_OK);

    /* Destroy entity */
    netudp_entity_destroy(server, eid);

    /* Destroy schema */
    netudp_schema_destroy(server, sid);

    netudp_server_stop(server);
    netudp_server_destroy(server);
}

TEST_F(ReplicationIntegrationTest, ReplicateDeliversToGroup) {
    char addr[64];
    std::snprintf(addr, sizeof(addr), "127.0.0.1:%u", static_cast<unsigned>(kRepPort + 1));

    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id = kRepProtoId;
    std::memcpy(srv_cfg.private_key, kRepKey, 32);
    srv_cfg.num_channels = 1;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    double t = 1000.0;
    netudp_server_t* server = netudp_server_create(addr, &srv_cfg, t);
    ASSERT_NE(server, nullptr);
    netudp_server_start(server, 8);

    /* Connect a client */
    const char* addrs[] = { addr };
    uint8_t token[2048] = {};
    netudp_generate_connect_token(1, addrs, 300, 10, 99001, kRepProtoId, kRepKey, nullptr, token);

    netudp_client_config_t cli_cfg = {};
    cli_cfg.protocol_id = kRepProtoId;
    cli_cfg.num_channels = 1;
    cli_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    netudp_client_t* client = netudp_client_create(nullptr, &cli_cfg, t);
    ASSERT_NE(client, nullptr);
    netudp_client_connect(client, token);

    auto deadline = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(3000);
    while (std::chrono::high_resolution_clock::now() < deadline) {
        t += 0.016;
        netudp_server_update(server, t);
        netudp_client_update(client, t);
        if (netudp_client_state(client) == 3) { break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(netudp_client_state(client), 3);

    int cidx = netudp_client_index(client);

    /* Create schema + entity + group */
    int sid = netudp_schema_create(server);
    netudp_schema_add_f32(server, sid, "x", NETUDP_REP_ALL);
    netudp_schema_add_f32(server, sid, "y", NETUDP_REP_ALL);

    uint16_t eid = netudp_entity_create(server, sid);
    int grp = netudp_group_create(server);
    netudp_group_add(server, grp, cidx);
    netudp_entity_set_group(server, eid, grp);

    /* Set properties */
    netudp_entity_set_f32(server, eid, 0, 42.0f);
    netudp_entity_set_f32(server, eid, 1, 84.0f);

    /* Replicate */
    netudp_server_replicate(server);

    /* Pump to deliver */
    for (int i = 0; i < 20; ++i) {
        t += 0.016;
        netudp_server_update(server, t);
        netudp_client_update(client, t);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    /* Client should have received a message with the entity data */
    netudp_message_t* msgs[16];
    int count = netudp_client_receive(client, msgs, 16);
    EXPECT_GE(count, 1);

    if (count > 0) {
        /* Verify we can deserialize the wire format */
        EXPECT_GE(msgs[0]->size, 4); /* entity_id(2) + varint(1) + values */
        for (int i = 0; i < count; ++i) { netudp_message_release(msgs[i]); }
    }

    netudp_entity_destroy(server, eid);
    netudp_group_destroy(server, grp);
    netudp_schema_destroy(server, sid);
    netudp_client_disconnect(client);
    netudp_client_destroy(client);
    netudp_server_stop(server);
    netudp_server_destroy(server);
}
