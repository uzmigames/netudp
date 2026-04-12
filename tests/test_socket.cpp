#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include "../src/socket/socket.h"

#include <cstring>
#include <thread>
#include <chrono>

class SocketTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

TEST_F(SocketTest, CreateAndDestroy) {
    netudp_address_t addr = {};
    netudp_parse_address("127.0.0.1:19000", &addr);

    netudp::Socket sock;
    int err = netudp::socket_create(&sock, &addr, 4 * 1024 * 1024, 4 * 1024 * 1024);
    ASSERT_EQ(err, NETUDP_OK);
    EXPECT_NE(sock.handle, NETUDP_INVALID_SOCKET);

    netudp::socket_destroy(&sock);
    EXPECT_EQ(sock.handle, NETUDP_INVALID_SOCKET);
}

TEST_F(SocketTest, SendToSelfIPv4) {
    netudp_address_t bind_addr = {};
    netudp_parse_address("127.0.0.1:19001", &bind_addr);

    netudp::Socket sock;
    ASSERT_EQ(netudp::socket_create(&sock, &bind_addr, 4 * 1024 * 1024, 4 * 1024 * 1024), NETUDP_OK);

    /* Send to self */
    const char msg[] = "Hello netudp";
    netudp_address_t dest = {};
    netudp_parse_address("127.0.0.1:19001", &dest);

    int sent = netudp::socket_send(&sock, &dest, msg, sizeof(msg));
    EXPECT_EQ(sent, static_cast<int>(sizeof(msg)));

    /* Small delay for loopback */
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    /* Receive */
    char buf[256] = {};
    netudp_address_t from = {};
    int received = netudp::socket_recv(&sock, &from, buf, sizeof(buf));
    ASSERT_GT(received, 0);
    EXPECT_EQ(received, static_cast<int>(sizeof(msg)));
    EXPECT_STREQ(buf, "Hello netudp");
    EXPECT_EQ(from.type, NETUDP_ADDRESS_IPV4);
    EXPECT_EQ(from.port, 19001);

    netudp::socket_destroy(&sock);
}

TEST_F(SocketTest, NonBlockingReturnsZeroWhenEmpty) {
    netudp_address_t addr = {};
    netudp_parse_address("127.0.0.1:19002", &addr);

    netudp::Socket sock;
    ASSERT_EQ(netudp::socket_create(&sock, &addr, 4 * 1024 * 1024, 4 * 1024 * 1024), NETUDP_OK);

    char buf[64] = {};
    netudp_address_t from = {};
    int received = netudp::socket_recv(&sock, &from, buf, sizeof(buf));
    EXPECT_EQ(received, 0); /* Non-blocking, no data */

    netudp::socket_destroy(&sock);
}

TEST_F(SocketTest, TwoSocketsCommunicate) {
    netudp_address_t addr_a = {}, addr_b = {};
    netudp_parse_address("127.0.0.1:19003", &addr_a);
    netudp_parse_address("127.0.0.1:19004", &addr_b);

    netudp::Socket sock_a, sock_b;
    ASSERT_EQ(netudp::socket_create(&sock_a, &addr_a, 4 * 1024 * 1024, 4 * 1024 * 1024), NETUDP_OK);
    ASSERT_EQ(netudp::socket_create(&sock_b, &addr_b, 4 * 1024 * 1024, 4 * 1024 * 1024), NETUDP_OK);

    /* A sends to B */
    const char msg[] = "ping";
    EXPECT_GT(netudp::socket_send(&sock_a, &addr_b, msg, sizeof(msg)), 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    /* B receives from A */
    char buf[64] = {};
    netudp_address_t from = {};
    int received = netudp::socket_recv(&sock_b, &from, buf, sizeof(buf));
    ASSERT_GT(received, 0);
    EXPECT_STREQ(buf, "ping");
    EXPECT_EQ(from.port, 19003); /* From A's port */

    netudp::socket_destroy(&sock_a);
    netudp::socket_destroy(&sock_b);
}
