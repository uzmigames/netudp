/**
 * @file test_groups.cpp
 * @brief Tests for multicast groups (phase 40).
 *
 * Verifies:
 * - Group create/destroy lifecycle
 * - Add/remove members with O(1) swap-remove
 * - group_send delivers to all members
 * - group_send_except skips one client
 * - Auto-removal on client disconnect
 * - Multiple groups per client
 */

#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <chrono>
#include <cstring>
#include <thread>

static constexpr uint64_t kGrpProtoId = 0x6770000000000001ULL;
static constexpr uint16_t kGrpPort    = 29850U;

static const uint8_t kGrpKey[32] = {
    0x40, 0x41, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
};

class GroupTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

static netudp_server_t* make_grp_server(uint16_t port, double sim_time) {
    char addr[64];
    std::snprintf(addr, sizeof(addr), "127.0.0.1:%u", static_cast<unsigned>(port));

    netudp_server_config_t cfg = {};
    cfg.protocol_id = kGrpProtoId;
    std::memcpy(cfg.private_key, kGrpKey, 32);
    cfg.num_channels = 1;
    cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    netudp_server_t* s = netudp_server_create(addr, &cfg, sim_time);
    if (s != nullptr) {
        netudp_server_start(s, 16);
    }
    return s;
}

static netudp_client_t* make_grp_client(uint16_t port, uint64_t client_id, double sim_time) {
    char addr[64];
    std::snprintf(addr, sizeof(addr), "127.0.0.1:%u", static_cast<unsigned>(port));

    const char* addrs[] = { addr };
    uint8_t token[2048] = {};
    netudp_generate_connect_token(1, addrs, 300, 10,
                                  client_id, kGrpProtoId, kGrpKey, nullptr, token);

    netudp_client_config_t cfg = {};
    cfg.protocol_id = kGrpProtoId;
    cfg.num_channels = 1;
    cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    netudp_client_t* c = netudp_client_create(nullptr, &cfg, sim_time);
    if (c != nullptr) {
        netudp_client_connect(c, token);
    }
    return c;
}

static bool pump(netudp_server_t* s, netudp_client_t* c, double& t, int ms = 3000) {
    auto deadline = std::chrono::high_resolution_clock::now() +
                    std::chrono::milliseconds(ms);
    while (std::chrono::high_resolution_clock::now() < deadline) {
        t += 0.016;
        netudp_server_update(s, t);
        netudp_client_update(c, t);
        if (netudp_client_state(c) == 3) { return true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

/* ---- Tests ---- */

TEST_F(GroupTest, CreateAndDestroy) {
    auto* s = make_grp_server(kGrpPort, 1000.0);
    ASSERT_NE(s, nullptr);

    int g1 = netudp_group_create(s);
    EXPECT_GE(g1, 0);

    int g2 = netudp_group_create(s);
    EXPECT_GE(g2, 0);
    EXPECT_NE(g1, g2);

    EXPECT_EQ(netudp_group_count(s, g1), 0);
    EXPECT_EQ(netudp_group_count(s, g2), 0);

    netudp_group_destroy(s, g1);
    netudp_group_destroy(s, g2);

    /* Destroyed group returns 0 count */
    EXPECT_EQ(netudp_group_count(s, g1), 0);

    netudp_server_stop(s);
    netudp_server_destroy(s);
}

TEST_F(GroupTest, AddRemoveMembers) {
    auto* s = make_grp_server(kGrpPort + 1, 1000.0);
    ASSERT_NE(s, nullptr);

    double t = 1000.0;
    auto* c1 = make_grp_client(kGrpPort + 1, 90001, t);
    auto* c2 = make_grp_client(kGrpPort + 1, 90002, t);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);
    ASSERT_TRUE(pump(s, c1, t));
    ASSERT_TRUE(pump(s, c2, t));

    int idx1 = netudp_client_index(c1);
    int idx2 = netudp_client_index(c2);

    int g = netudp_group_create(s);
    ASSERT_GE(g, 0);

    /* Add members */
    EXPECT_EQ(netudp_group_add(s, g, idx1), NETUDP_OK);
    EXPECT_EQ(netudp_group_add(s, g, idx2), NETUDP_OK);
    EXPECT_EQ(netudp_group_count(s, g), 2);
    EXPECT_EQ(netudp_group_has(s, g, idx1), 1);
    EXPECT_EQ(netudp_group_has(s, g, idx2), 1);

    /* Double-add returns error */
    EXPECT_NE(netudp_group_add(s, g, idx1), NETUDP_OK);

    /* Remove one */
    EXPECT_EQ(netudp_group_remove(s, g, idx1), NETUDP_OK);
    EXPECT_EQ(netudp_group_count(s, g), 1);
    EXPECT_EQ(netudp_group_has(s, g, idx1), 0);
    EXPECT_EQ(netudp_group_has(s, g, idx2), 1);

    /* Remove non-member returns error */
    EXPECT_NE(netudp_group_remove(s, g, idx1), NETUDP_OK);

    netudp_group_destroy(s, g);
    netudp_client_disconnect(c1);
    netudp_client_disconnect(c2);
    netudp_client_destroy(c1);
    netudp_client_destroy(c2);
    netudp_server_stop(s);
    netudp_server_destroy(s);
}

TEST_F(GroupTest, GroupSendDeliversToMembers) {
    auto* s = make_grp_server(kGrpPort + 2, 1000.0);
    ASSERT_NE(s, nullptr);

    double t = 1000.0;
    auto* c1 = make_grp_client(kGrpPort + 2, 90010, t);
    auto* c2 = make_grp_client(kGrpPort + 2, 90011, t);
    auto* c3 = make_grp_client(kGrpPort + 2, 90012, t);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);
    ASSERT_NE(c3, nullptr);
    ASSERT_TRUE(pump(s, c1, t));
    ASSERT_TRUE(pump(s, c2, t));
    ASSERT_TRUE(pump(s, c3, t));

    int idx1 = netudp_client_index(c1);
    int idx2 = netudp_client_index(c2);
    int idx3 = netudp_client_index(c3);

    /* Create group with c1 and c2 only (c3 not in group) */
    int g = netudp_group_create(s);
    netudp_group_add(s, g, idx1);
    netudp_group_add(s, g, idx2);

    /* Send via group */
    uint8_t payload[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    netudp_group_send(s, g, 0, payload, 8, NETUDP_SEND_NO_DELAY);

    /* Pump to deliver */
    for (int i = 0; i < 20; ++i) {
        t += 0.016;
        netudp_server_update(s, t);
        netudp_client_update(c1, t);
        netudp_client_update(c2, t);
        netudp_client_update(c3, t);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    /* c1 and c2 should receive, c3 should not */
    netudp_message_t* msgs[4];

    int n1 = netudp_client_receive(c1, msgs, 4);
    EXPECT_GE(n1, 1);
    for (int i = 0; i < n1; ++i) { netudp_message_release(msgs[i]); }

    int n2 = netudp_client_receive(c2, msgs, 4);
    EXPECT_GE(n2, 1);
    for (int i = 0; i < n2; ++i) { netudp_message_release(msgs[i]); }

    int n3 = netudp_client_receive(c3, msgs, 4);
    EXPECT_EQ(n3, 0);

    netudp_group_destroy(s, g);
    netudp_client_disconnect(c1);
    netudp_client_disconnect(c2);
    netudp_client_disconnect(c3);
    netudp_client_destroy(c1);
    netudp_client_destroy(c2);
    netudp_client_destroy(c3);
    netudp_server_stop(s);
    netudp_server_destroy(s);
}

TEST_F(GroupTest, GroupSendExceptSkipsOwner) {
    auto* s = make_grp_server(kGrpPort + 3, 1000.0);
    ASSERT_NE(s, nullptr);

    double t = 1000.0;
    auto* c1 = make_grp_client(kGrpPort + 3, 90020, t);
    auto* c2 = make_grp_client(kGrpPort + 3, 90021, t);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);
    ASSERT_TRUE(pump(s, c1, t));
    ASSERT_TRUE(pump(s, c2, t));

    int idx1 = netudp_client_index(c1);
    int idx2 = netudp_client_index(c2);

    int g = netudp_group_create(s);
    netudp_group_add(s, g, idx1);
    netudp_group_add(s, g, idx2);

    /* Send except c1 (owner) — only c2 should receive */
    uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    netudp_group_send_except(s, g, idx1, 0, payload, 4, NETUDP_SEND_NO_DELAY);

    for (int i = 0; i < 20; ++i) {
        t += 0.016;
        netudp_server_update(s, t);
        netudp_client_update(c1, t);
        netudp_client_update(c2, t);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    netudp_message_t* msgs[4];
    int n1 = netudp_client_receive(c1, msgs, 4);
    EXPECT_EQ(n1, 0); /* Owner skipped */

    int n2 = netudp_client_receive(c2, msgs, 4);
    EXPECT_GE(n2, 1);
    for (int i = 0; i < n2; ++i) { netudp_message_release(msgs[i]); }

    netudp_group_destroy(s, g);
    netudp_client_disconnect(c1);
    netudp_client_disconnect(c2);
    netudp_client_destroy(c1);
    netudp_client_destroy(c2);
    netudp_server_stop(s);
    netudp_server_destroy(s);
}

TEST_F(GroupTest, AutoRemoveOnDisconnect) {
    auto* s = make_grp_server(kGrpPort + 4, 1000.0);
    ASSERT_NE(s, nullptr);

    double t = 1000.0;
    auto* c1 = make_grp_client(kGrpPort + 4, 90030, t);
    ASSERT_NE(c1, nullptr);
    ASSERT_TRUE(pump(s, c1, t));

    int idx1 = netudp_client_index(c1);
    int g = netudp_group_create(s);
    netudp_group_add(s, g, idx1);
    EXPECT_EQ(netudp_group_count(s, g), 1);
    EXPECT_EQ(netudp_group_has(s, g, idx1), 1);

    /* Disconnect client — should auto-remove from group */
    netudp_client_disconnect(c1);
    netudp_client_destroy(c1);

    /* Pump server to process timeout/disconnect */
    for (int i = 0; i < 200; ++i) {
        t += 0.1;
        netudp_server_update(s, t);
    }

    /* Client should be removed from group */
    EXPECT_EQ(netudp_group_count(s, g), 0);
    EXPECT_EQ(netudp_group_has(s, g, idx1), 0);

    netudp_group_destroy(s, g);
    netudp_server_stop(s);
    netudp_server_destroy(s);
}

TEST_F(GroupTest, MultipleGroupsPerClient) {
    auto* s = make_grp_server(kGrpPort + 5, 1000.0);
    ASSERT_NE(s, nullptr);

    double t = 1000.0;
    auto* c1 = make_grp_client(kGrpPort + 5, 90040, t);
    ASSERT_NE(c1, nullptr);
    ASSERT_TRUE(pump(s, c1, t));

    int idx = netudp_client_index(c1);

    int zone  = netudp_group_create(s);
    int party = netudp_group_create(s);
    int guild = netudp_group_create(s);
    ASSERT_GE(zone, 0);
    ASSERT_GE(party, 0);
    ASSERT_GE(guild, 0);

    /* Client belongs to all three groups */
    EXPECT_EQ(netudp_group_add(s, zone, idx), NETUDP_OK);
    EXPECT_EQ(netudp_group_add(s, party, idx), NETUDP_OK);
    EXPECT_EQ(netudp_group_add(s, guild, idx), NETUDP_OK);

    EXPECT_EQ(netudp_group_has(s, zone, idx), 1);
    EXPECT_EQ(netudp_group_has(s, party, idx), 1);
    EXPECT_EQ(netudp_group_has(s, guild, idx), 1);

    /* Remove from party only */
    netudp_group_remove(s, party, idx);
    EXPECT_EQ(netudp_group_has(s, zone, idx), 1);
    EXPECT_EQ(netudp_group_has(s, party, idx), 0);
    EXPECT_EQ(netudp_group_has(s, guild, idx), 1);

    netudp_group_destroy(s, zone);
    netudp_group_destroy(s, party);
    netudp_group_destroy(s, guild);
    netudp_client_disconnect(c1);
    netudp_client_destroy(c1);
    netudp_server_stop(s);
    netudp_server_destroy(s);
}

TEST_F(GroupTest, InvalidParamsReturnErrors) {
    auto* s = make_grp_server(kGrpPort + 6, 1000.0);
    ASSERT_NE(s, nullptr);

    /* Invalid group_id */
    EXPECT_EQ(netudp_group_count(s, -1), 0);
    EXPECT_EQ(netudp_group_count(s, 9999), 0);
    EXPECT_EQ(netudp_group_has(s, -1, 0), 0);
    EXPECT_NE(netudp_group_add(s, -1, 0), NETUDP_OK);
    EXPECT_NE(netudp_group_remove(s, -1, 0), NETUDP_OK);

    /* Valid group, invalid client */
    int g = netudp_group_create(s);
    EXPECT_NE(netudp_group_add(s, g, -1), NETUDP_OK);
    EXPECT_NE(netudp_group_add(s, g, 9999), NETUDP_OK);

    /* Add disconnected client */
    EXPECT_NE(netudp_group_add(s, g, 0), NETUDP_OK);

    netudp_group_destroy(s, g);
    netudp_server_stop(s);
    netudp_server_destroy(s);
}

TEST_F(GroupTest, ReuseDestroyedGroupSlot) {
    auto* s = make_grp_server(kGrpPort + 7, 1000.0);
    ASSERT_NE(s, nullptr);

    int g1 = netudp_group_create(s);
    netudp_group_destroy(s, g1);

    /* Should be able to create again and get the same slot */
    int g2 = netudp_group_create(s);
    EXPECT_EQ(g2, g1); /* Free stack returns same index */

    netudp_group_destroy(s, g2);
    netudp_server_stop(s);
    netudp_server_destroy(s);
}
