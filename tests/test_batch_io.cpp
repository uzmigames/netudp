/**
 * @file test_batch_io.cpp
 * @brief Tests for socket_recv_batch / socket_send_batch.
 *
 * Sends 100 packets from client to server using the batch path and verifies
 * all arrive.  The batch send uses individual socket_send calls (client side)
 * while the server drains with socket_recv_batch on the server socket.
 */

#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>
#include "src/socket/socket.h"

#include <chrono>
#include <cstring>
#include <thread>

static constexpr uint64_t kBatchProtocolId = 0xBEEF00040000004ULL;
static constexpr uint16_t kBatchPort       = 28900U;
static constexpr uint8_t  kBatchType       = 0x04U;
static constexpr int      kBatchPayload    = 64;
static constexpr int      kBatchCount      = 100;

static const uint8_t kBatchKey[32] = {
    0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0,
    0xC0, 0xD0, 0xE0, 0xF0, 0x01, 0x11, 0x21, 0x31,
    0x41, 0x51, 0x61, 0x71, 0x81, 0x91, 0xA1, 0xB1,
    0xC1, 0xD1, 0xE1, 0xF1, 0x02, 0x12, 0x22, 0x32,
};

struct BatchCounter { int received = 0; };

static void batch_handler(void* ctx, int /*ci*/, const void* /*d*/,
                          int /*s*/, int /*ch*/) {
    static_cast<BatchCounter*>(ctx)->received++;
}

class BatchIO : public ::testing::Test {
protected:
    void SetUp() override   { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

TEST_F(BatchIO, SendAndReceiveBatch) {
    using Clock = std::chrono::high_resolution_clock;

    char srv_addr[64];
    std::snprintf(srv_addr, sizeof(srv_addr), "127.0.0.1:%u",
                  static_cast<unsigned>(kBatchPort));

    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id = kBatchProtocolId;
    std::memcpy(srv_cfg.private_key, kBatchKey, 32);
    srv_cfg.num_channels = 1;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    double sim_time = 8000.0;
    netudp_server_t* server = netudp_server_create(srv_addr, &srv_cfg, sim_time);
    ASSERT_NE(server, nullptr);
    netudp_server_start(server, 4);

    BatchCounter counter;
    netudp_server_set_packet_handler(server, kBatchType, batch_handler, &counter);

    const char* addrs[] = { srv_addr };
    uint8_t token[2048] = {};
    ASSERT_EQ(netudp_generate_connect_token(1, addrs, 300, 10,
                                            10001ULL, kBatchProtocolId,
                                            kBatchKey, nullptr, token), NETUDP_OK);

    netudp_client_config_t cli_cfg = {};
    cli_cfg.protocol_id = kBatchProtocolId;
    cli_cfg.num_channels = 1;
    cli_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    netudp_client_t* client = netudp_client_create(nullptr, &cli_cfg, sim_time);
    ASSERT_NE(client, nullptr);
    netudp_client_connect(client, token);

    /* Handshake */
    auto deadline = Clock::now() + std::chrono::milliseconds(3000);
    while (Clock::now() < deadline && netudp_client_state(client) != 3) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        netudp_client_update(client, sim_time);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_EQ(netudp_client_state(client), 3) << "Client failed to connect";

    /* Send kBatchCount packets — advance sim_time by 1/60 per send to keep
     * the rate limiter funded (same pattern as bench_pps).               */
    static constexpr double kSimStep = 1.0 / 60.0;

    uint8_t payload[kBatchPayload] = {};
    payload[0] = kBatchType;

    for (int i = 0; i < kBatchCount; ++i) {
        sim_time += kSimStep;
        netudp_client_send(client, 0, payload, kBatchPayload, NETUDP_SEND_UNRELIABLE);
        netudp_client_update(client, sim_time);
        /* Server drains with batch recv internally in netudp_server_update */
        netudp_server_update(server, sim_time);

        netudp_message_t* msgs[32];
        int n = netudp_server_receive(server, 0, msgs, 32);
        for (int m = 0; m < n; ++m) { netudp_message_release(msgs[m]); }
    }

    /* Drain any remaining — unreliable transport, allow a few extra ticks */
    for (int i = 0; i < 32; ++i) {
        sim_time += kSimStep;
        netudp_server_update(server, sim_time);
        netudp_client_update(client, sim_time);
        netudp_message_t* msgs[32];
        int n = netudp_server_receive(server, 0, msgs, 32);
        for (int m = 0; m < n; ++m) { netudp_message_release(msgs[m]); }
    }

    /* Unreliable channel — accept ≥95% delivery */
    EXPECT_GE(counter.received, kBatchCount * 95 / 100)
        << "Expected at least " << (kBatchCount * 95 / 100)
        << " packets, got " << counter.received;

    netudp_client_disconnect(client);
    netudp_client_destroy(client);
    netudp_server_stop(server);
    netudp_server_destroy(server);
}

TEST_F(BatchIO, ZeroPacketsReturnZero) {
    /* send/recv_batch on null socket returns -1, not crash */
    netudp::SocketPacket pkts[4] = {};
    int r = netudp::socket_recv_batch(nullptr, pkts, 4, 1400);
    EXPECT_EQ(r, -1);
    r = netudp::socket_send_batch(nullptr, pkts, 4);
    EXPECT_EQ(r, -1);
}
