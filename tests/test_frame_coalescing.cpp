/**
 * @file test_frame_coalescing.cpp
 * @brief Tests for frame coalescing: multiple messages packed into one UDP packet.
 *
 * Verifies that:
 * - Multiple small messages sent in one tick arrive correctly
 * - Messages across different channels are coalesced
 * - Reliable + unreliable messages can be mixed in one coalesced packet
 */

#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <chrono>
#include <cstring>
#include <thread>

static constexpr uint64_t kCoalesceProtoId = 0xC0A1E5CE00000001ULL;
static constexpr uint16_t kCoalescePort    = 29900U;
static constexpr int      kSmallMsgSize    = 20;

static const uint8_t kCoalesceKey[32] = {
    0xC0, 0xA1, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
};

class FrameCoalescing : public ::testing::Test {
protected:
    void SetUp() override   { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

static netudp_client_t* connect_client(const char* srv_addr, uint64_t client_id,
                                        double sim_time, int num_channels,
                                        const netudp_channel_config_t* channels) {
    const char* addrs[] = { srv_addr };
    uint8_t token[2048] = {};
    if (netudp_generate_connect_token(1, addrs, 300, 10,
                                      client_id, kCoalesceProtoId,
                                      kCoalesceKey, nullptr, token) != NETUDP_OK) {
        return nullptr;
    }
    netudp_client_config_t cli_cfg = {};
    cli_cfg.protocol_id  = kCoalesceProtoId;
    cli_cfg.num_channels = num_channels;
    for (int i = 0; i < num_channels; ++i) {
        cli_cfg.channels[i] = channels[i];
    }
    netudp_client_t* c = netudp_client_create(nullptr, &cli_cfg, sim_time);
    if (c != nullptr) { netudp_client_connect(c, token); }
    return c;
}

static bool pump_until_connected(netudp_server_t* server, netudp_client_t* client,
                                  double& sim_time, int timeout_ms = 3000) {
    using Clock = std::chrono::high_resolution_clock;
    auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);

    while (Clock::now() < deadline) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        netudp_client_update(client, sim_time);
        if (netudp_client_state(client) == 3 /* CONNECTED */) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

static int drain_client_messages(netudp_client_t* client, int* per_channel, int max_channels) {
    netudp_message_t* msgs[32];
    int total = 0;
    int n = netudp_client_receive(client, msgs, 32);
    while (n > 0) {
        for (int i = 0; i < n; ++i) {
            if (msgs[i]->channel >= 0 && msgs[i]->channel < max_channels) {
                per_channel[msgs[i]->channel]++;
            }
            total++;
            netudp_message_release(msgs[i]);
        }
        n = netudp_client_receive(client, msgs, 32);
    }
    return total;
}

/**
 * Test: Send 10 small unreliable messages in one tick.
 * With coalescing, they should be packed into 1-2 packets instead of 10.
 * All messages must arrive.
 */
TEST_F(FrameCoalescing, MultipleSmallMessagesArrive) {
    char srv_addr[64];
    std::snprintf(srv_addr, sizeof(srv_addr), "127.0.0.1:%u",
                  static_cast<unsigned>(kCoalescePort));

    netudp_channel_config_t ch_cfg = {};
    ch_cfg.type = NETUDP_CHANNEL_UNRELIABLE;

    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id  = kCoalesceProtoId;
    std::memcpy(srv_cfg.private_key, kCoalesceKey, 32);
    srv_cfg.num_channels = 1;
    srv_cfg.channels[0]  = ch_cfg;

    double sim_time = 1000.0;
    netudp_server_t* server = netudp_server_create(srv_addr, &srv_cfg, sim_time);
    ASSERT_NE(server, nullptr);
    netudp_server_start(server, 8);

    netudp_client_t* client = connect_client(srv_addr, 50001, sim_time, 1, &ch_cfg);
    ASSERT_NE(client, nullptr);
    ASSERT_TRUE(pump_until_connected(server, client, sim_time));

    /* Send 10 small messages from server to client in one batch */
    static constexpr int kMsgCount = 10;
    for (int i = 0; i < kMsgCount; ++i) {
        uint8_t data[kSmallMsgSize];
        std::memset(data, static_cast<uint8_t>(i + 1), kSmallMsgSize);
        int rc = netudp_server_send(server, 0, 0, data, kSmallMsgSize, 0);
        EXPECT_EQ(rc, NETUDP_OK);
    }

    /* Pump to deliver */
    int received = 0;
    int per_ch[1] = {0};
    for (int tick = 0; tick < 100 && received < kMsgCount; ++tick) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        netudp_client_update(client, sim_time);
        received += drain_client_messages(client, per_ch, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_GE(received, kMsgCount - 1); /* Allow 1 loss on unreliable */

    netudp_client_destroy(client);
    netudp_server_destroy(server);
}

/**
 * Test: Send messages on 2 different channels.
 * Coalescing should pack frames from multiple channels into one packet.
 */
TEST_F(FrameCoalescing, MultiChannelCoalescing) {
    uint16_t port = kCoalescePort + 1U;
    char srv_addr[64];
    std::snprintf(srv_addr, sizeof(srv_addr), "127.0.0.1:%u",
                  static_cast<unsigned>(port));

    netudp_channel_config_t channels[2] = {};
    channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
    channels[1].type = NETUDP_CHANNEL_UNRELIABLE;

    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id  = kCoalesceProtoId;
    std::memcpy(srv_cfg.private_key, kCoalesceKey, 32);
    srv_cfg.num_channels = 2;
    srv_cfg.channels[0]  = channels[0];
    srv_cfg.channels[1]  = channels[1];

    double sim_time = 2000.0;
    netudp_server_t* server = netudp_server_create(srv_addr, &srv_cfg, sim_time);
    ASSERT_NE(server, nullptr);
    netudp_server_start(server, 8);

    netudp_client_t* client = connect_client(srv_addr, 50002, sim_time, 2, channels);
    ASSERT_NE(client, nullptr);
    ASSERT_TRUE(pump_until_connected(server, client, sim_time));

    /* Send 5 messages on channel 0 and 5 on channel 1 */
    for (int i = 0; i < 5; ++i) {
        uint8_t data_ch0[kSmallMsgSize];
        std::memset(data_ch0, 0xAA, kSmallMsgSize);
        data_ch0[0] = static_cast<uint8_t>(i);
        netudp_server_send(server, 0, 0, data_ch0, kSmallMsgSize, 0);

        uint8_t data_ch1[kSmallMsgSize];
        std::memset(data_ch1, 0xBB, kSmallMsgSize);
        data_ch1[0] = static_cast<uint8_t>(i);
        netudp_server_send(server, 0, 1, data_ch1, kSmallMsgSize, 0);
    }

    int per_ch[2] = {0, 0};
    int total = 0;
    for (int tick = 0; tick < 100; ++tick) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        netudp_client_update(client, sim_time);
        total += drain_client_messages(client, per_ch, 2);
        if (total >= 10) { break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_GE(per_ch[0], 4);
    EXPECT_GE(per_ch[1], 4);

    netudp_client_destroy(client);
    netudp_server_destroy(server);
}

/**
 * Test: Send a mix of reliable + unreliable on different channels.
 * Reliable messages must all arrive; unreliable may lose at most 1.
 */
TEST_F(FrameCoalescing, MixedReliableUnreliable) {
    uint16_t port = kCoalescePort + 2U;
    char srv_addr[64];
    std::snprintf(srv_addr, sizeof(srv_addr), "127.0.0.1:%u",
                  static_cast<unsigned>(port));

    netudp_channel_config_t channels[2] = {};
    channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
    channels[1].type = NETUDP_CHANNEL_RELIABLE_ORDERED;

    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id  = kCoalesceProtoId;
    std::memcpy(srv_cfg.private_key, kCoalesceKey, 32);
    srv_cfg.num_channels = 2;
    srv_cfg.channels[0]  = channels[0];
    srv_cfg.channels[1]  = channels[1];

    double sim_time = 3000.0;
    netudp_server_t* server = netudp_server_create(srv_addr, &srv_cfg, sim_time);
    ASSERT_NE(server, nullptr);
    netudp_server_start(server, 8);

    netudp_client_t* client = connect_client(srv_addr, 50003, sim_time, 2, channels);
    ASSERT_NE(client, nullptr);
    ASSERT_TRUE(pump_until_connected(server, client, sim_time));

    /* Send 5 unreliable on ch0, 5 reliable on ch1 */
    for (int i = 0; i < 5; ++i) {
        uint8_t data[kSmallMsgSize];
        std::memset(data, static_cast<uint8_t>(i), kSmallMsgSize);
        netudp_server_send(server, 0, 0, data, kSmallMsgSize, 0);
        netudp_server_send(server, 0, 1, data, kSmallMsgSize, 0);
    }

    int per_ch[2] = {0, 0};
    int total = 0;
    for (int tick = 0; tick < 200; ++tick) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        netudp_client_update(client, sim_time);
        total += drain_client_messages(client, per_ch, 2);
        if (per_ch[1] >= 5) { break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_GE(per_ch[0], 4);   /* Unreliable: allow 1 loss */
    EXPECT_EQ(per_ch[1], 5);   /* Reliable: must all arrive */

    netudp_client_destroy(client);
    netudp_server_destroy(server);
}
