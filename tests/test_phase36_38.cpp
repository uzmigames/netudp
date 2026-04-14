/**
 * @file test_phase36_38.cpp
 * @brief Tests for phase 36 (slot_id in packet header) and phase 38 (cached sockaddr).
 *
 * Phase 38: socket_send_raw with pre-built sockaddr sends correctly.
 * Phase 36: client embeds slot_id in data packets, server dispatches via O(1) array lookup,
 *           fallback to hash map when slot_id is invalid.
 */

#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>
#include "../src/socket/socket.h"
#include "../src/connection/connection.h"

#include <chrono>
#include <cstring>
#include <thread>

/* ======================================================================
 * Phase 38: socket_send_raw tests
 * ====================================================================== */

class CachedSockaddrTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

TEST_F(CachedSockaddrTest, SendRawIPv4) {
    netudp_address_t srv_addr = {};
    netudp_parse_address("127.0.0.1:29800", &srv_addr);

    netudp_address_t cli_addr = {};
    netudp_parse_address("127.0.0.1:29801", &cli_addr);

    netudp::Socket srv, cli;
    ASSERT_EQ(netudp::socket_create(&srv, &srv_addr, 4*1024*1024, 4*1024*1024), NETUDP_OK);
    ASSERT_EQ(netudp::socket_create(&cli, &cli_addr, 4*1024*1024, 4*1024*1024), NETUDP_OK);

    /* Pre-build sockaddr (what cached_sa does) */
    uint8_t cached_sa[128] = {};
    int cached_sa_len = 0;
    netudp::address_to_sockaddr(&srv_addr, cached_sa, &cached_sa_len);

    /* Send using socket_send_raw — no address_to_sockaddr per send */
    const char msg[] = "cached_send_raw";
    int sent = netudp::socket_send_raw(&cli, cached_sa, cached_sa_len,
                                        msg, static_cast<int>(sizeof(msg)));
    EXPECT_EQ(sent, static_cast<int>(sizeof(msg)));

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    /* Receive on server */
    char buf[256] = {};
    netudp_address_t from = {};
    int received = netudp::socket_recv(&srv, &from, buf, sizeof(buf));
    ASSERT_GT(received, 0);
    EXPECT_STREQ(buf, "cached_send_raw");
    EXPECT_EQ(from.port, 29801);

    netudp::socket_destroy(&srv);
    netudp::socket_destroy(&cli);
}

TEST_F(CachedSockaddrTest, SendRawIPv6) {
    netudp_address_t srv_addr = {};
    netudp_parse_address("[::1]:29802", &srv_addr);

    netudp_address_t cli_addr = {};
    netudp_parse_address("[::1]:29803", &cli_addr);

    netudp::Socket srv, cli;
    int srv_ok = netudp::socket_create(&srv, &srv_addr, 4*1024*1024, 4*1024*1024);
    int cli_ok = netudp::socket_create(&cli, &cli_addr, 4*1024*1024, 4*1024*1024);

    if (srv_ok != NETUDP_OK || cli_ok != NETUDP_OK) {
        /* IPv6 may not be available on all CI machines */
        if (srv_ok == NETUDP_OK) netudp::socket_destroy(&srv);
        if (cli_ok == NETUDP_OK) netudp::socket_destroy(&cli);
        GTEST_SKIP() << "IPv6 not available";
    }

    uint8_t cached_sa[128] = {};
    int cached_sa_len = 0;
    netudp::address_to_sockaddr(&srv_addr, cached_sa, &cached_sa_len);

    const char msg[] = "ipv6_raw";
    int sent = netudp::socket_send_raw(&cli, cached_sa, cached_sa_len,
                                        msg, static_cast<int>(sizeof(msg)));
    EXPECT_EQ(sent, static_cast<int>(sizeof(msg)));

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    char buf[256] = {};
    netudp_address_t from = {};
    int received = netudp::socket_recv(&srv, &from, buf, sizeof(buf));
    ASSERT_GT(received, 0);
    EXPECT_STREQ(buf, "ipv6_raw");

    netudp::socket_destroy(&srv);
    netudp::socket_destroy(&cli);
}

TEST_F(CachedSockaddrTest, CachedSaInConnectionReset) {
    netudp::Connection conn;
    /* Populate cached_sa with some data */
    netudp_address_t addr = {};
    netudp_parse_address("127.0.0.1:9999", &addr);
    netudp::address_to_sockaddr(&addr, conn.cached_sa, &conn.cached_sa_len);
    EXPECT_GT(conn.cached_sa_len, 0);

    /* Reset should clear it */
    conn.reset();
    EXPECT_EQ(conn.cached_sa_len, 0);

    /* Verify the buffer is zeroed */
    uint8_t zeroes[128] = {};
    EXPECT_EQ(std::memcmp(conn.cached_sa, zeroes, sizeof(zeroes)), 0);
}

TEST_F(CachedSockaddrTest, NullSocketReturnsError) {
    uint8_t sa[128] = {};
    int sa_len = sizeof(struct sockaddr_in);
    int result = netudp::socket_send_raw(nullptr, sa, sa_len, "x", 1);
    EXPECT_EQ(result, -1);
}

/* ======================================================================
 * Phase 36: slot_id in wire header tests
 * ====================================================================== */

static constexpr uint64_t kSlotProtoId = 0x5107100000000001ULL;
static constexpr uint16_t kSlotPort    = 29810U;

static const uint8_t kSlotKey[32] = {
    0x36, 0x38, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
};

class SlotIdTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

static netudp_client_t* make_slot_client(const char* srv_addr, uint64_t client_id,
                                          double sim_time) {
    const char* addrs[] = { srv_addr };
    uint8_t token[2048] = {};
    if (netudp_generate_connect_token(1, addrs, 300, 10,
                                      client_id, kSlotProtoId,
                                      kSlotKey, nullptr, token) != NETUDP_OK) {
        return nullptr;
    }
    netudp_client_config_t cfg = {};
    cfg.protocol_id  = kSlotProtoId;
    cfg.num_channels = 1;
    cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
    netudp_client_t* c = netudp_client_create(nullptr, &cfg, sim_time);
    if (c != nullptr) { netudp_client_connect(c, token); }
    return c;
}

static bool pump_connected(netudp_server_t* server, netudp_client_t* client,
                            double& sim_time, int timeout_ms = 3000) {
    auto deadline = std::chrono::high_resolution_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (std::chrono::high_resolution_clock::now() < deadline) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        netudp_client_update(client, sim_time);
        if (netudp_client_state(client) == 3) { return true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

TEST_F(SlotIdTest, ClientReceivesSlotIdOnConnect) {
    char addr[64];
    std::snprintf(addr, sizeof(addr), "127.0.0.1:%u", static_cast<unsigned>(kSlotPort));

    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id = kSlotProtoId;
    std::memcpy(srv_cfg.private_key, kSlotKey, 32);
    srv_cfg.num_channels = 1;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    double sim_time = 1000.0;
    netudp_server_t* server = netudp_server_create(addr, &srv_cfg, sim_time);
    ASSERT_NE(server, nullptr);
    netudp_server_start(server, 8);

    netudp_client_t* client = make_slot_client(addr, 70001, sim_time);
    ASSERT_NE(client, nullptr);
    ASSERT_TRUE(pump_connected(server, client, sim_time));

    /* Client should have a valid client_index (= slot_id assigned by server) */
    int idx = netudp_client_index(client);
    EXPECT_GE(idx, 0);
    EXPECT_LT(idx, 8);

    netudp_client_disconnect(client);
    netudp_client_destroy(client);
    netudp_server_stop(server);
    netudp_server_destroy(server);
}

TEST_F(SlotIdTest, DataFlowsWithSlotIdHeader) {
    char addr[64];
    std::snprintf(addr, sizeof(addr), "127.0.0.1:%u",
                  static_cast<unsigned>(kSlotPort + 1U));

    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id = kSlotProtoId;
    std::memcpy(srv_cfg.private_key, kSlotKey, 32);
    srv_cfg.num_channels = 1;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    double sim_time = 1000.0;
    netudp_server_t* server = netudp_server_create(addr, &srv_cfg, sim_time);
    ASSERT_NE(server, nullptr);
    netudp_server_start(server, 8);

    netudp_client_t* client = make_slot_client(addr, 70002, sim_time);
    ASSERT_NE(client, nullptr);
    ASSERT_TRUE(pump_connected(server, client, sim_time));

    /* Pump a couple extra ticks to ensure client keepalive is acked */
    for (int i = 0; i < 5; ++i) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        netudp_client_update(client, sim_time);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    /* Client sends data to server (client→server includes slot_id in header) */
    uint8_t payload[32];
    std::memset(payload, 0xAB, sizeof(payload));
    int send_ok = netudp_client_send(client, 0, payload, sizeof(payload), NETUDP_SEND_NO_DELAY);
    EXPECT_EQ(send_ok, NETUDP_OK);

    /* Pump ticks so packet is sent + received */
    for (int i = 0; i < 20; ++i) {
        sim_time += 0.016;
        netudp_client_update(client, sim_time);
        netudp_server_update(server, sim_time);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    /* Server should have received the message */
    int client_idx = netudp_client_index(client);
    netudp_message_t* msgs[4];
    int count = netudp_server_receive(server, client_idx, msgs, 4);
    EXPECT_GE(count, 1);

    if (count > 0) {
        EXPECT_EQ(msgs[0]->size, static_cast<int>(sizeof(payload)));
        uint8_t expected[32];
        std::memset(expected, 0xAB, sizeof(expected));
        EXPECT_EQ(std::memcmp(msgs[0]->data, expected, sizeof(expected)), 0);

        for (int i = 0; i < count; ++i) {
            netudp_message_release(msgs[i]);
        }
    }

    /* Server sends data to client (server→client, no slot_id in header) */
    uint8_t reply[16];
    std::memset(reply, 0xCD, sizeof(reply));
    netudp_server_send(server, client_idx, 0, reply, sizeof(reply), NETUDP_SEND_NO_DELAY);

    for (int i = 0; i < 10; ++i) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        netudp_client_update(client, sim_time);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    netudp_message_t* cli_msgs[4];
    int cli_count = netudp_client_receive(client, cli_msgs, 4);
    EXPECT_GE(cli_count, 1);

    if (cli_count > 0) {
        EXPECT_EQ(cli_msgs[0]->size, static_cast<int>(sizeof(reply)));
        uint8_t expected_reply[16];
        std::memset(expected_reply, 0xCD, sizeof(expected_reply));
        EXPECT_EQ(std::memcmp(cli_msgs[0]->data, expected_reply, sizeof(expected_reply)), 0);

        for (int i = 0; i < cli_count; ++i) {
            netudp_message_release(cli_msgs[i]);
        }
    }

    netudp_client_disconnect(client);
    netudp_client_destroy(client);
    netudp_server_stop(server);
    netudp_server_destroy(server);
}

TEST_F(SlotIdTest, MultipleClientsGetDifferentSlotIds) {
    char addr[64];
    std::snprintf(addr, sizeof(addr), "127.0.0.1:%u",
                  static_cast<unsigned>(kSlotPort + 2U));

    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id = kSlotProtoId;
    std::memcpy(srv_cfg.private_key, kSlotKey, 32);
    srv_cfg.num_channels = 1;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    double sim_time = 1000.0;
    netudp_server_t* server = netudp_server_create(addr, &srv_cfg, sim_time);
    ASSERT_NE(server, nullptr);
    netudp_server_start(server, 8);

    /* Connect 3 clients */
    netudp_client_t* clients[3];
    for (int i = 0; i < 3; ++i) {
        clients[i] = make_slot_client(addr, static_cast<uint64_t>(80001 + i), sim_time);
        ASSERT_NE(clients[i], nullptr);
        ASSERT_TRUE(pump_connected(server, clients[i], sim_time));
    }

    /* All should have valid and unique slot_ids */
    int slots[3];
    for (int i = 0; i < 3; ++i) {
        slots[i] = netudp_client_index(clients[i]);
        EXPECT_GE(slots[i], 0);
        EXPECT_LT(slots[i], 8);
    }
    EXPECT_NE(slots[0], slots[1]);
    EXPECT_NE(slots[0], slots[2]);
    EXPECT_NE(slots[1], slots[2]);

    for (int i = 0; i < 3; ++i) {
        netudp_client_disconnect(clients[i]);
        netudp_client_destroy(clients[i]);
    }
    netudp_server_stop(server);
    netudp_server_destroy(server);
}

TEST_F(SlotIdTest, ConnectionSlotIdReset) {
    netudp::Connection conn;
    conn.slot_id = 42;
    EXPECT_EQ(conn.slot_id, 42);

    conn.reset();
    EXPECT_EQ(conn.slot_id, 0xFFFF);
}
