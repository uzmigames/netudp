#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include "../src/core/address.h"

#include <cstring>

class AddressTest : public ::testing::Test {
protected:
    void SetUp() override { netudp_init(); }
    void TearDown() override { netudp_term(); }
};

/* ===== Parsing ===== */

TEST_F(AddressTest, ParseIPv4) {
    netudp_address_t addr = {};
    ASSERT_EQ(netudp_parse_address("192.168.1.100:7777", &addr), NETUDP_OK);
    EXPECT_EQ(addr.type, NETUDP_ADDRESS_IPV4);
    EXPECT_EQ(addr.port, 7777);
    EXPECT_EQ(addr.data.ipv4[0], 192);
    EXPECT_EQ(addr.data.ipv4[1], 168);
    EXPECT_EQ(addr.data.ipv4[2], 1);
    EXPECT_EQ(addr.data.ipv4[3], 100);
}

TEST_F(AddressTest, ParseIPv4Localhost) {
    netudp_address_t addr = {};
    ASSERT_EQ(netudp_parse_address("127.0.0.1:27015", &addr), NETUDP_OK);
    EXPECT_EQ(addr.type, NETUDP_ADDRESS_IPV4);
    EXPECT_EQ(addr.port, 27015);
    EXPECT_EQ(addr.data.ipv4[0], 127);
    EXPECT_EQ(addr.data.ipv4[3], 1);
}

TEST_F(AddressTest, ParseIPv4BindAll) {
    netudp_address_t addr = {};
    ASSERT_EQ(netudp_parse_address("0.0.0.0:7777", &addr), NETUDP_OK);
    EXPECT_EQ(addr.type, NETUDP_ADDRESS_IPV4);
    EXPECT_EQ(addr.port, 7777);
}

TEST_F(AddressTest, ParseIPv6Loopback) {
    netudp_address_t addr = {};
    ASSERT_EQ(netudp_parse_address("[::1]:27015", &addr), NETUDP_OK);
    EXPECT_EQ(addr.type, NETUDP_ADDRESS_IPV6);
    EXPECT_EQ(addr.port, 27015);
    EXPECT_EQ(addr.data.ipv6[7], 1);
}

TEST_F(AddressTest, ParseIPv6BindAll) {
    netudp_address_t addr = {};
    ASSERT_EQ(netudp_parse_address("[::]:7777", &addr), NETUDP_OK);
    EXPECT_EQ(addr.type, NETUDP_ADDRESS_IPV6);
    EXPECT_EQ(addr.port, 7777);
}

TEST_F(AddressTest, ParseIPv6Full) {
    netudp_address_t addr = {};
    ASSERT_EQ(netudp_parse_address("[2001:db8:0:0:0:0:0:1]:8080", &addr), NETUDP_OK);
    EXPECT_EQ(addr.type, NETUDP_ADDRESS_IPV6);
    EXPECT_EQ(addr.port, 8080);
    EXPECT_EQ(addr.data.ipv6[0], 0x2001);
    EXPECT_EQ(addr.data.ipv6[1], 0x0db8);
    EXPECT_EQ(addr.data.ipv6[7], 1);
}

TEST_F(AddressTest, ParseInvalidNull) {
    EXPECT_EQ(netudp_parse_address(nullptr, nullptr), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(AddressTest, ParseInvalidNoPort) {
    netudp_address_t addr = {};
    EXPECT_EQ(netudp_parse_address("192.168.1.1", &addr), NETUDP_ERROR_INVALID_PARAM);
}

TEST_F(AddressTest, ParseInvalidBadOctet) {
    netudp_address_t addr = {};
    EXPECT_EQ(netudp_parse_address("256.0.0.1:7777", &addr), NETUDP_ERROR_INVALID_PARAM);
}

/* ===== Formatting ===== */

TEST_F(AddressTest, FormatIPv4) {
    netudp_address_t addr = {};
    netudp_parse_address("10.0.0.1:9999", &addr);

    char buf[64] = {};
    netudp_address_to_string(&addr, buf, sizeof(buf));
    EXPECT_STREQ(buf, "10.0.0.1:9999");
}

TEST_F(AddressTest, FormatIPv6Loopback) {
    netudp_address_t addr = {};
    netudp_parse_address("[::1]:27015", &addr);

    char buf[64] = {};
    netudp_address_to_string(&addr, buf, sizeof(buf));
    EXPECT_STREQ(buf, "[0:0:0:0:0:0:0:1]:27015");
}

/* ===== Equality ===== */

TEST_F(AddressTest, EqualSameIPv4) {
    netudp_address_t a = {}, b = {};
    netudp_parse_address("192.168.1.1:7777", &a);
    netudp_parse_address("192.168.1.1:7777", &b);
    EXPECT_EQ(netudp_address_equal(&a, &b), 1);
}

TEST_F(AddressTest, NotEqualDifferentIP) {
    netudp_address_t a = {}, b = {};
    netudp_parse_address("192.168.1.1:7777", &a);
    netudp_parse_address("192.168.1.2:7777", &b);
    EXPECT_EQ(netudp_address_equal(&a, &b), 0);
}

TEST_F(AddressTest, NotEqualDifferentPort) {
    netudp_address_t a = {}, b = {};
    netudp_parse_address("192.168.1.1:7777", &a);
    netudp_parse_address("192.168.1.1:8888", &b);
    EXPECT_EQ(netudp_address_equal(&a, &b), 0);
}

TEST_F(AddressTest, NotEqualDifferentType) {
    netudp_address_t a = {}, b = {};
    netudp_parse_address("127.0.0.1:7777", &a);
    netudp_parse_address("[::1]:7777", &b);
    EXPECT_EQ(netudp_address_equal(&a, &b), 0);
}

/* ===== Hash ===== */

TEST_F(AddressTest, HashConsistency) {
    netudp_address_t a = {}, b = {};
    netudp_parse_address("10.0.0.1:5000", &a);
    netudp_parse_address("10.0.0.1:5000", &b);
    EXPECT_EQ(netudp::address_hash(&a), netudp::address_hash(&b));
}

TEST_F(AddressTest, HashDifferentForDifferentAddresses) {
    netudp_address_t a = {}, b = {};
    netudp_parse_address("10.0.0.1:5000", &a);
    netudp_parse_address("10.0.0.2:5000", &b);
    EXPECT_NE(netudp::address_hash(&a), netudp::address_hash(&b));
}

/* ===== Round-trip ===== */

TEST_F(AddressTest, RoundTripIPv4) {
    netudp_address_t a = {};
    netudp_parse_address("172.16.0.1:12345", &a);

    char buf[64] = {};
    netudp_address_to_string(&a, buf, sizeof(buf));

    netudp_address_t b = {};
    ASSERT_EQ(netudp_parse_address(buf, &b), NETUDP_OK);
    EXPECT_EQ(netudp_address_equal(&a, &b), 1);
}
