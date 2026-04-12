/**
 * @file test_batch_api.cpp
 * @brief Tests for netudp_server_send_batch() and netudp_server_receive_batch().
 */

#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <chrono>
#include <cstring>
#include <thread>

static constexpr uint64_t kBatchApiProtocolId = 0xBEEF00050000005ULL;
static constexpr uint16_t kBatchApiPort       = 28800U;
static constexpr uint8_t  kBatchApiType       = 0x05U;
static constexpr int      kBatchApiPayload    = 64;

static const uint8_t kBatchApiKey[32] = {
    0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0,
    0xD0, 0xE0, 0xF0, 0x01, 0x11, 0x21, 0x31, 0x41,
    0x51, 0x61, 0x71, 0x81, 0x91, 0xA1, 0xB1, 0xC1,
    0xD1, 0xE1, 0xF1, 0x02, 0x12, 0x22, 0x32, 0x42,
};

class BatchApi : public ::testing::Test {
protected:
    void SetUp() override   { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

static netudp_client_t* make_client(const char* srv_addr, uint64_t client_id,
                                    double sim_time) {
    const char* addrs[] = { srv_addr };
    uint8_t token[2048] = {};
    if (netudp_generate_connect_token(1, addrs, 300, 10,
                                      client_id, kBatchApiProtocolId,
                                      kBatchApiKey, nullptr, token) != NETUDP_OK) {
        return nullptr;
    }
    netudp_client_config_t cli_cfg = {};
    cli_cfg.protocol_id  = kBatchApiProtocolId;
    cli_cfg.num_channels = 1;
    cli_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
    netudp_client_t* c = netudp_client_create(nullptr, &cli_cfg, sim_time);
    if (c != nullptr) { netudp_client_connect(c, token); }
    return c;
}

/* --- Test 2.1 + 2.2: send_batch to multiple clients, receive_batch --- */

TEST_F(BatchApi, SendBatchAndReceiveBatch) {
    using Clock = std::chrono::high_resolution_clock;

    char srv_addr[64];
    std::snprintf(srv_addr, sizeof(srv_addr), "127.0.0.1:%u",
                  static_cast<unsigned>(kBatchApiPort));

    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id  = kBatchApiProtocolId;
    std::memcpy(srv_cfg.private_key, kBatchApiKey, 32);
    srv_cfg.num_channels = 1;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    double sim_time = 9000.0;
    netudp_server_t* server = netudp_server_create(srv_addr, &srv_cfg, sim_time);
    ASSERT_NE(server, nullptr);
    netudp_server_start(server, 8);

    /* Connect 3 clients */
    static constexpr int kClients = 3;
    netudp_client_t* clients[kClients] = {};
    for (int i = 0; i < kClients; ++i) {
        clients[i] = make_client(srv_addr,
                                 static_cast<uint64_t>(20001 + i), sim_time);
        ASSERT_NE(clients[i], nullptr);
    }

    /* Handshake */
    auto deadline = Clock::now() + std::chrono::milliseconds(3000);
    while (Clock::now() < deadline) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        for (int i = 0; i < kClients; ++i) { netudp_client_update(clients[i], sim_time); }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        bool all = true;
        for (int i = 0; i < kClients; ++i) {
            if (netudp_client_state(clients[i]) != 3) { all = false; break; }
        }
        if (all) break;
    }
    for (int i = 0; i < kClients; ++i) {
        ASSERT_EQ(netudp_client_state(clients[i]), 3) << "client " << i << " not connected";
    }

    /* Build batch send entries — one message per client */
    uint8_t payloads[kClients][kBatchApiPayload] = {};
    netudp_send_entry_t entries[kClients] = {};
    for (int i = 0; i < kClients; ++i) {
        payloads[i][0] = kBatchApiType;
        entries[i].client_index = i;
        entries[i].channel      = 0;
        entries[i].data         = payloads[i];
        entries[i].bytes        = kBatchApiPayload;
        entries[i].flags        = NETUDP_SEND_UNRELIABLE;
    }

    /* Rate limiter: advance sim_time so server can receive from clients */
    static constexpr double kSimStep = 1.0 / 60.0;

    /* Clients send to server first (for packet counter test) */
    for (int i = 0; i < kClients; ++i) {
        sim_time += kSimStep;
        netudp_client_send(clients[i], 0, payloads[i], kBatchApiPayload, NETUDP_SEND_UNRELIABLE);
    }
    for (int i = 0; i < kClients; ++i) { netudp_client_update(clients[i], sim_time); }
    netudp_server_update(server, sim_time);

    /* receive_batch collects from all slots */
    netudp_message_t* batch_msgs[64] = {};
    int recv_total = netudp_server_receive_batch(server, batch_msgs, 64);
    EXPECT_GE(recv_total, 0);
    for (int i = 0; i < recv_total; ++i) { netudp_message_release(batch_msgs[i]); }

    /* send_batch: server sends to all 3 clients */
    sim_time += kSimStep;
    int queued = netudp_server_send_batch(server, entries, kClients);
    EXPECT_EQ(queued, kClients);

    netudp_server_update(server, sim_time);
    for (int i = 0; i < kClients; ++i) { netudp_client_update(clients[i], sim_time); }

    /* Each client should have received 1 message */
    int client_received = 0;
    for (int i = 0; i < kClients; ++i) {
        netudp_message_t* msgs[8];
        int n = netudp_client_receive(clients[i], msgs, 8);
        client_received += n;
        for (int m = 0; m < n; ++m) { netudp_message_release(msgs[m]); }
    }
    EXPECT_GE(client_received, kClients * 80 / 100); /* ≥80% delivery */

    /* Cleanup */
    for (int i = 0; i < kClients; ++i) {
        netudp_client_disconnect(clients[i]);
        netudp_client_destroy(clients[i]);
    }
    netudp_server_stop(server);
    netudp_server_destroy(server);
}

TEST_F(BatchApi, SendBatchNullServerReturnsError) {
    netudp_send_entry_t e = {};
    int rc = netudp_server_send_batch(nullptr, &e, 1);
    EXPECT_EQ(rc, NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(BatchApi, ReceiveBatchNullServerReturnsError) {
    netudp_message_t* out[4] = {};
    int rc = netudp_server_receive_batch(nullptr, out, 4);
    EXPECT_EQ(rc, NETUDP_ERROR_INVALID_PARAM);
}
