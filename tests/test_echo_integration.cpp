#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include "../src/socket/socket.h"
#include "../src/core/pool.h"

#include <cstring>
#include <thread>
#include <chrono>

/**
 * Integration test: raw UDP echo using Socket + Pool<T> + Address.
 * Validates that all Phase 1 subsystems work together.
 */

struct PacketBuffer {
    uint8_t data[1400];
    int     len;
};

class EchoIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

TEST_F(EchoIntegrationTest, RawUDPEcho) {
    /* Server socket */
    netudp_address_t server_addr = {};
    netudp_parse_address("127.0.0.1:19100", &server_addr);

    netudp::Socket server_sock;
    ASSERT_EQ(netudp::socket_create(&server_sock, &server_addr,
              4 * 1024 * 1024, 4 * 1024 * 1024), NETUDP_OK);

    /* Client socket */
    netudp_address_t client_addr = {};
    netudp_parse_address("127.0.0.1:19101", &client_addr);

    netudp::Socket client_sock;
    ASSERT_EQ(netudp::socket_create(&client_sock, &client_addr,
              4 * 1024 * 1024, 4 * 1024 * 1024), NETUDP_OK);

    /* Pool for server recv buffers */
    netudp::Pool<PacketBuffer> pool;
    ASSERT_TRUE(pool.init(16));

    /* Client sends "Hello" to server */
    const char hello[] = "Hello netudp echo";
    int sent = netudp::socket_send(&client_sock, &server_addr,
                                    hello, static_cast<int>(sizeof(hello)));
    EXPECT_EQ(sent, static_cast<int>(sizeof(hello)));

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    /* Server receives into pool buffer */
    PacketBuffer* recv_buf = pool.acquire();
    ASSERT_NE(recv_buf, nullptr);

    netudp_address_t from = {};
    recv_buf->len = netudp::socket_recv(&server_sock, &from,
                                         recv_buf->data, sizeof(recv_buf->data));
    ASSERT_GT(recv_buf->len, 0);
    EXPECT_EQ(recv_buf->len, static_cast<int>(sizeof(hello)));
    EXPECT_EQ(std::memcmp(recv_buf->data, hello, sizeof(hello)), 0);

    /* Verify sender address */
    EXPECT_EQ(from.type, NETUDP_ADDRESS_IPV4);
    EXPECT_EQ(from.port, 19101);

    /* Server echoes back to client */
    int echoed = netudp::socket_send(&server_sock, &from,
                                      recv_buf->data, recv_buf->len);
    EXPECT_EQ(echoed, recv_buf->len);

    pool.release(recv_buf);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    /* Client receives echo */
    char echo_buf[256] = {};
    netudp_address_t echo_from = {};
    int echo_len = netudp::socket_recv(&client_sock, &echo_from,
                                        echo_buf, sizeof(echo_buf));
    ASSERT_GT(echo_len, 0);
    EXPECT_STREQ(echo_buf, "Hello netudp echo");
    EXPECT_EQ(echo_from.port, 19100);

    /* Pool is back to full capacity */
    EXPECT_EQ(pool.available(), 16);

    netudp::socket_destroy(&server_sock);
    netudp::socket_destroy(&client_sock);
}

TEST_F(EchoIntegrationTest, MultipleMessagesWithPool) {
    netudp_address_t srv_addr = {}, cli_addr = {};
    netudp_parse_address("127.0.0.1:19102", &srv_addr);
    netudp_parse_address("127.0.0.1:19103", &cli_addr);

    netudp::Socket srv, cli;
    ASSERT_EQ(netudp::socket_create(&srv, &srv_addr, 4*1024*1024, 4*1024*1024), NETUDP_OK);
    ASSERT_EQ(netudp::socket_create(&cli, &cli_addr, 4*1024*1024, 4*1024*1024), NETUDP_OK);

    netudp::Pool<PacketBuffer> pool;
    ASSERT_TRUE(pool.init(8));

    /* Send 5 messages */
    for (int i = 0; i < 5; ++i) {
        char msg[32] = {};
        std::snprintf(msg, sizeof(msg), "msg_%d", i);
        netudp::socket_send(&cli, &srv_addr, msg, static_cast<int>(std::strlen(msg) + 1));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    /* Receive all 5 */
    int received_count = 0;
    for (int attempt = 0; attempt < 10; ++attempt) {
        PacketBuffer* buf = pool.acquire();
        if (buf == nullptr) {
            break;
        }

        netudp_address_t from = {};
        buf->len = netudp::socket_recv(&srv, &from, buf->data, sizeof(buf->data));
        if (buf->len <= 0) {
            pool.release(buf);
            break;
        }

        ++received_count;

        /* Echo back */
        netudp::socket_send(&srv, &from, buf->data, buf->len);
        pool.release(buf);
    }

    EXPECT_EQ(received_count, 5);
    EXPECT_EQ(pool.available(), 8); /* All returned to pool */

    netudp::socket_destroy(&srv);
    netudp::socket_destroy(&cli);
}
