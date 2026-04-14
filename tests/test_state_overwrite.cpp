/**
 * @file test_state_overwrite.cpp
 * @brief Tests for state overwrite / latest-wins per entity_id (phase 41).
 *
 * Verifies:
 * - Multiple sends with same entity_id overwrite (only latest delivered)
 * - Different entity_ids coexist in queue
 * - State overwrite rejected on reliable channels
 * - group_send_state delivers to members with overwrite
 */

#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <chrono>
#include <cstring>
#include <thread>

static constexpr uint64_t kSoProtoId = 0x5041000000000001ULL;
static constexpr uint16_t kSoPort    = 29870U;

static const uint8_t kSoKey[32] = {
    0x41, 0x42, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
};

class StateOverwriteTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

struct TestSetup {
    netudp_server_t* server = nullptr;
    netudp_client_t* client = nullptr;
    int client_idx = -1;
    double sim_time = 1000.0;
};

static TestSetup connect_one(uint16_t port, uint64_t client_id) {
    TestSetup s;
    char addr[64];
    std::snprintf(addr, sizeof(addr), "127.0.0.1:%u", static_cast<unsigned>(port));

    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id = kSoProtoId;
    std::memcpy(srv_cfg.private_key, kSoKey, 32);
    srv_cfg.num_channels = 2;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
    srv_cfg.channels[1].type = NETUDP_CHANNEL_RELIABLE_ORDERED;

    s.server = netudp_server_create(addr, &srv_cfg, s.sim_time);
    netudp_server_start(s.server, 8);

    const char* addrs[] = { addr };
    uint8_t token[2048] = {};
    netudp_generate_connect_token(1, addrs, 300, 10, client_id, kSoProtoId, kSoKey, nullptr, token);

    netudp_client_config_t cli_cfg = {};
    cli_cfg.protocol_id = kSoProtoId;
    cli_cfg.num_channels = 2;
    cli_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
    cli_cfg.channels[1].type = NETUDP_CHANNEL_RELIABLE_ORDERED;

    s.client = netudp_client_create(nullptr, &cli_cfg, s.sim_time);
    netudp_client_connect(s.client, token);

    auto deadline = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(3000);
    while (std::chrono::high_resolution_clock::now() < deadline) {
        s.sim_time += 0.016;
        netudp_server_update(s.server, s.sim_time);
        netudp_client_update(s.client, s.sim_time);
        if (netudp_client_state(s.client) == 3) { break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    s.client_idx = netudp_client_index(s.client);
    return s;
}

static void teardown(TestSetup& s) {
    netudp_client_disconnect(s.client);
    netudp_client_destroy(s.client);
    netudp_server_stop(s.server);
    netudp_server_destroy(s.server);
}

TEST_F(StateOverwriteTest, LatestWinsPerEntityId) {
    auto s = connect_one(kSoPort, 95001);
    ASSERT_EQ(netudp_client_state(s.client), 3);

    /* Send 3 updates for entity 42 — only the last should arrive */
    uint8_t p1[4] = {0x01, 0x00, 0x00, 0x00};
    uint8_t p2[4] = {0x02, 0x00, 0x00, 0x00};
    uint8_t p3[4] = {0x03, 0x00, 0x00, 0x00};

    netudp_server_send_state(s.server, s.client_idx, 0, 42, p1, 4);
    netudp_server_send_state(s.server, s.client_idx, 0, 42, p2, 4);
    netudp_server_send_state(s.server, s.client_idx, 0, 42, p3, 4);

    /* Flush and deliver */
    netudp_server_flush(s.server, s.client_idx);

    for (int i = 0; i < 20; ++i) {
        s.sim_time += 0.016;
        netudp_server_update(s.server, s.sim_time);
        netudp_client_update(s.client, s.sim_time);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    netudp_message_t* msgs[16];
    int count = netudp_client_receive(s.client, msgs, 16);

    /* Should receive exactly 1 message (the last one, p3) */
    EXPECT_EQ(count, 1);
    if (count >= 1) {
        EXPECT_EQ(msgs[0]->size, 4);
        EXPECT_EQ(static_cast<const uint8_t*>(msgs[0]->data)[0], 0x03);
        for (int i = 0; i < count; ++i) { netudp_message_release(msgs[i]); }
    }

    teardown(s);
}

TEST_F(StateOverwriteTest, DifferentEntitiesCoexist) {
    auto s = connect_one(kSoPort + 1, 95002);
    ASSERT_EQ(netudp_client_state(s.client), 3);

    /* Send updates for 3 different entities */
    uint8_t p1[4] = {0xA1, 0x00, 0x00, 0x00};
    uint8_t p2[4] = {0xB2, 0x00, 0x00, 0x00};
    uint8_t p3[4] = {0xC3, 0x00, 0x00, 0x00};

    netudp_server_send_state(s.server, s.client_idx, 0, 100, p1, 4);
    netudp_server_send_state(s.server, s.client_idx, 0, 200, p2, 4);
    netudp_server_send_state(s.server, s.client_idx, 0, 300, p3, 4);

    netudp_server_flush(s.server, s.client_idx);

    for (int i = 0; i < 20; ++i) {
        s.sim_time += 0.016;
        netudp_server_update(s.server, s.sim_time);
        netudp_client_update(s.client, s.sim_time);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    netudp_message_t* msgs[16];
    int count = netudp_client_receive(s.client, msgs, 16);

    /* All 3 entities should arrive (different entity_ids) */
    EXPECT_EQ(count, 3);
    for (int i = 0; i < count; ++i) { netudp_message_release(msgs[i]); }

    teardown(s);
}

TEST_F(StateOverwriteTest, ReliableChannelRejectsState) {
    auto s = connect_one(kSoPort + 2, 95003);
    ASSERT_EQ(netudp_client_state(s.client), 3);

    uint8_t p[4] = {0x01, 0x02, 0x03, 0x04};

    /* Channel 1 is reliable_ordered — should reject send_state */
    int result = netudp_server_send_state(s.server, s.client_idx, 1, 42, p, 4);
    EXPECT_NE(result, NETUDP_OK);

    /* Channel 0 is unreliable — should accept */
    result = netudp_server_send_state(s.server, s.client_idx, 0, 42, p, 4);
    EXPECT_EQ(result, NETUDP_OK);

    teardown(s);
}

TEST_F(StateOverwriteTest, OverwriteWithDifferentSize) {
    auto s = connect_one(kSoPort + 3, 95004);
    ASSERT_EQ(netudp_client_state(s.client), 3);

    /* First send: 4 bytes. Second send: 8 bytes. Latest should win with new size. */
    uint8_t p1[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t p2[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

    netudp_server_send_state(s.server, s.client_idx, 0, 50, p1, 4);
    netudp_server_send_state(s.server, s.client_idx, 0, 50, p2, 8);

    netudp_server_flush(s.server, s.client_idx);

    for (int i = 0; i < 20; ++i) {
        s.sim_time += 0.016;
        netudp_server_update(s.server, s.sim_time);
        netudp_client_update(s.client, s.sim_time);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    netudp_message_t* msgs[16];
    int count = netudp_client_receive(s.client, msgs, 16);
    EXPECT_EQ(count, 1);
    if (count >= 1) {
        EXPECT_EQ(msgs[0]->size, 8);
        EXPECT_EQ(static_cast<const uint8_t*>(msgs[0]->data)[0], 0x11);
        for (int i = 0; i < count; ++i) { netudp_message_release(msgs[i]); }
    }

    teardown(s);
}

TEST_F(StateOverwriteTest, GroupSendStateOverwrites) {
    auto s = connect_one(kSoPort + 4, 95005);
    ASSERT_EQ(netudp_client_state(s.client), 3);

    int g = netudp_group_create(s.server);
    ASSERT_GE(g, 0);
    netudp_group_add(s.server, g, s.client_idx);

    /* Send 3 updates via group for same entity — only last should arrive */
    uint8_t p1[4] = {0x10, 0, 0, 0};
    uint8_t p2[4] = {0x20, 0, 0, 0};
    uint8_t p3[4] = {0x30, 0, 0, 0};

    netudp_group_send_state(s.server, g, 0, 99, p1, 4);
    netudp_group_send_state(s.server, g, 0, 99, p2, 4);
    netudp_group_send_state(s.server, g, 0, 99, p3, 4);

    netudp_server_flush(s.server, s.client_idx);

    for (int i = 0; i < 20; ++i) {
        s.sim_time += 0.016;
        netudp_server_update(s.server, s.sim_time);
        netudp_client_update(s.client, s.sim_time);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    netudp_message_t* msgs[16];
    int count = netudp_client_receive(s.client, msgs, 16);
    EXPECT_EQ(count, 1);
    if (count >= 1) {
        EXPECT_EQ(static_cast<const uint8_t*>(msgs[0]->data)[0], 0x30);
        for (int i = 0; i < count; ++i) { netudp_message_release(msgs[i]); }
    }

    netudp_group_destroy(s.server, g);
    teardown(s);
}
