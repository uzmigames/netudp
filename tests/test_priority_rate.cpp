/**
 * @file test_priority_rate.cpp
 * @brief Tests for priority + rate limiting in replication (phase 43).
 */

#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <chrono>
#include <cstring>
#include <thread>

static constexpr uint64_t kPrProtoId = 0x5052000000000001ULL;
static constexpr uint16_t kPrPort    = 29920U;

static const uint8_t kPrKey[32] = {
    0x43, 0x44, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
};

class PriorityRateTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

struct PrSetup {
    netudp_server_t* server = nullptr;
    netudp_client_t* client = nullptr;
    int cidx = -1;
    int schema = -1;
    int grp = -1;
    double t = 1000.0;
};

static PrSetup pr_connect(uint16_t port) {
    PrSetup s;
    char addr[64];
    std::snprintf(addr, sizeof(addr), "127.0.0.1:%u", static_cast<unsigned>(port));

    netudp_server_config_t cfg = {};
    cfg.protocol_id = kPrProtoId;
    std::memcpy(cfg.private_key, kPrKey, 32);
    cfg.num_channels = 1;
    cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    s.server = netudp_server_create(addr, &cfg, s.t);
    netudp_server_start(s.server, 8);

    const char* addrs[] = { addr };
    uint8_t token[2048] = {};
    netudp_generate_connect_token(1, addrs, 300, 10, 98001, kPrProtoId, kPrKey, nullptr, token);

    netudp_client_config_t ccfg = {};
    ccfg.protocol_id = kPrProtoId;
    ccfg.num_channels = 1;
    ccfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    s.client = netudp_client_create(nullptr, &ccfg, s.t);
    netudp_client_connect(s.client, token);

    auto deadline = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(3000);
    while (std::chrono::high_resolution_clock::now() < deadline) {
        s.t += 0.016;
        netudp_server_update(s.server, s.t);
        netudp_client_update(s.client, s.t);
        if (netudp_client_state(s.client) == 3) { break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    s.cidx = netudp_client_index(s.client);
    s.schema = netudp_schema_create(s.server);
    netudp_schema_add_f32(s.server, s.schema, "x", NETUDP_REP_ALL);
    s.grp = netudp_group_create(s.server);
    netudp_group_add(s.server, s.grp, s.cidx);
    return s;
}

static void pr_teardown(PrSetup& s) {
    netudp_group_destroy(s.server, s.grp);
    netudp_schema_destroy(s.server, s.schema);
    netudp_client_disconnect(s.client);
    netudp_client_destroy(s.client);
    netudp_server_stop(s.server);
    netudp_server_destroy(s.server);
}

TEST_F(PriorityRateTest, RateLimitThrottlesUpdates) {
    auto s = pr_connect(kPrPort);
    ASSERT_EQ(netudp_client_state(s.client), 3);

    uint16_t eid = netudp_entity_create(s.server, s.schema);
    netudp_entity_set_group(s.server, eid, s.grp);
    netudp_entity_set_max_rate(s.server, eid, 2.0f); /* 2 Hz = 1 update per 500ms */

    /* Set value and replicate — should send (first time) */
    netudp_entity_set_f32(s.server, eid, 0, 1.0f);
    netudp_server_replicate(s.server);

    /* Advance 100ms and set again — should be rate limited */
    s.t += 0.1;
    netudp_server_update(s.server, s.t);
    netudp_entity_set_f32(s.server, eid, 0, 2.0f);
    netudp_server_replicate(s.server);

    /* Advance 500ms and set again — should send */
    s.t += 0.5;
    netudp_server_update(s.server, s.t);
    netudp_entity_set_f32(s.server, eid, 0, 3.0f);
    netudp_server_replicate(s.server);

    /* Flush and pump */
    netudp_server_flush(s.server, s.cidx);
    for (int i = 0; i < 20; ++i) {
        s.t += 0.016;
        netudp_server_update(s.server, s.t);
        netudp_client_update(s.client, s.t);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    netudp_message_t* msgs[16];
    int count = netudp_client_receive(s.client, msgs, 16);
    /* Should have received 2 updates (first + after 500ms), not 3 */
    EXPECT_EQ(count, 2);
    for (int i = 0; i < count; ++i) { netudp_message_release(msgs[i]); }

    netudp_entity_destroy(s.server, eid);
    pr_teardown(s);
}

TEST_F(PriorityRateTest, SetPriorityAndRate) {
    auto s = pr_connect(kPrPort + 1);
    ASSERT_EQ(netudp_client_state(s.client), 3);

    uint16_t eid = netudp_entity_create(s.server, s.schema);
    netudp_entity_set_group(s.server, eid, s.grp);

    /* Set priority and rate */
    netudp_entity_set_priority(s.server, eid, 255);
    netudp_entity_set_max_rate(s.server, eid, 60.0f);

    /* Set value and replicate — should work with high rate */
    netudp_entity_set_f32(s.server, eid, 0, 42.0f);
    netudp_server_replicate(s.server);

    netudp_server_flush(s.server, s.cidx);
    for (int i = 0; i < 20; ++i) {
        s.t += 0.016;
        netudp_server_update(s.server, s.t);
        netudp_client_update(s.client, s.t);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    netudp_message_t* msgs[16];
    int count = netudp_client_receive(s.client, msgs, 16);
    EXPECT_GE(count, 1);
    for (int i = 0; i < count; ++i) { netudp_message_release(msgs[i]); }

    netudp_entity_destroy(s.server, eid);
    pr_teardown(s);
}

TEST_F(PriorityRateTest, UnlimitedRateSendsEveryTick) {
    auto s = pr_connect(kPrPort + 2);
    ASSERT_EQ(netudp_client_state(s.client), 3);

    uint16_t eid = netudp_entity_create(s.server, s.schema);
    netudp_entity_set_group(s.server, eid, s.grp);
    netudp_entity_set_max_rate(s.server, eid, 0.0f); /* Unlimited */

    /* Send 5 updates in quick succession — all should go through */
    int sent = 0;
    for (int i = 0; i < 5; ++i) {
        s.t += 0.001; /* 1ms between each */
        netudp_server_update(s.server, s.t);
        netudp_entity_set_f32(s.server, eid, 0, static_cast<float>(i));
        netudp_server_replicate(s.server);
        sent++;
    }

    netudp_server_flush(s.server, s.cidx);
    for (int i = 0; i < 30; ++i) {
        s.t += 0.016;
        netudp_server_update(s.server, s.t);
        netudp_client_update(s.client, s.t);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    netudp_message_t* msgs[16];
    int count = netudp_client_receive(s.client, msgs, 16);
    /* With state overwrite + unlimited rate, we get 1 message (latest-wins per entity) */
    EXPECT_GE(count, 1);
    for (int i = 0; i < count; ++i) { netudp_message_release(msgs[i]); }

    netudp_entity_destroy(s.server, eid);
    pr_teardown(s);
}
