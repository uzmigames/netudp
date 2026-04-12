#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <cstring>
#include <thread>
#include <chrono>
#include <ctime>

class HandshakeTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

static int g_connect_count = 0;
static uint64_t g_last_client_id = 0;

static void on_connect(void* /*ctx*/, int /*client_index*/, uint64_t client_id,
                        const uint8_t /*user_data*/[256]) {
    g_connect_count++;
    g_last_client_id = client_id;
}

static int g_disconnect_count = 0;

static void on_disconnect(void* /*ctx*/, int /*client_index*/, int /*reason*/) {
    g_disconnect_count++;
}

TEST_F(HandshakeTest, FullConnectionFlow) {
    g_connect_count = 0;
    g_last_client_id = 0;

    uint8_t private_key[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

    /* Create server */
    netudp_server_config_t server_cfg = {};
    server_cfg.protocol_id = 0xDEADBEEF;
    std::memcpy(server_cfg.private_key, private_key, 32);
    server_cfg.on_connect = on_connect;
    server_cfg.on_disconnect = on_disconnect;

    netudp_server_t* server = netudp_server_create("127.0.0.1:19400", &server_cfg, 1000.0);
    ASSERT_NE(server, nullptr);
    netudp_server_start(server, 64);

    /* Generate connect token */
    uint8_t token[2048] = {};
    const char* servers[] = {"127.0.0.1:19400"};
    int result = netudp_generate_connect_token(1, servers, 300, 10,
                                                42, 0xDEADBEEF,
                                                private_key, nullptr, token);
    ASSERT_EQ(result, NETUDP_OK);

    /* Create client */
    netudp_client_config_t client_cfg = {};
    client_cfg.protocol_id = 0xDEADBEEF;

    netudp_client_t* client = netudp_client_create(nullptr, &client_cfg, 1000.0);
    ASSERT_NE(client, nullptr);

    /* Connect */
    netudp_client_connect(client, token);
    EXPECT_EQ(netudp_client_state(client), 1); /* SENDING_REQUEST */

    /* Run update loop — client sends request, server processes it */
    double time = 1000.0;
    for (int i = 0; i < 20; ++i) {
        time += 0.016; /* ~60fps */
        netudp_server_update(server, time);
        netudp_client_update(client, time);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        if (netudp_client_state(client) == 3) { /* CONNECTED */
            break;
        }
    }

    EXPECT_EQ(netudp_client_state(client), 3); /* CONNECTED */
    EXPECT_EQ(g_connect_count, 1);
    EXPECT_EQ(g_last_client_id, 42U);

    /* Cleanup */
    netudp_client_disconnect(client);
    netudp_client_destroy(client);
    netudp_server_stop(server);
    netudp_server_destroy(server);
}

TEST_F(HandshakeTest, ServerCreateAndDestroy) {
    netudp_server_config_t cfg = {};
    cfg.protocol_id = 1;
    netudp_server_t* server = netudp_server_create("127.0.0.1:19401", &cfg, 0.0);
    ASSERT_NE(server, nullptr);
    netudp_server_start(server, 16);
    netudp_server_stop(server);
    netudp_server_destroy(server);
}

TEST_F(HandshakeTest, ClientCreateAndDestroy) {
    netudp_client_config_t cfg = {};
    cfg.protocol_id = 1;
    netudp_client_t* client = netudp_client_create(nullptr, &cfg, 0.0);
    ASSERT_NE(client, nullptr);
    netudp_client_destroy(client);
}

TEST_F(HandshakeTest, ServerBadAddress) {
    netudp_server_config_t cfg = {};
    EXPECT_EQ(netudp_server_create("not_valid", &cfg, 0.0), nullptr);
}

TEST_F(HandshakeTest, ClientNullConfig) {
    EXPECT_EQ(netudp_client_create(nullptr, nullptr, 0.0), nullptr);
}
