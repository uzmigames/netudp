#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include <netudp/netudp_buffer.h>
#include <netudp/netudp_config.h>
#include <cstring>

/* The buffer struct is defined in src/api.cpp.  Mirror it here for
 * white-box unit testing — must stay in sync with that definition.  */
struct netudp_buffer {
    uint8_t data[NETUDP_MTU];
    int     capacity;
    int     position;
};

/* Helper: reset buffer read position to 0 so write helpers
 * and read helpers can be tested on the same buffer.        */
static void buf_rewind(netudp_buffer_t* buf) {
    reinterpret_cast<netudp_buffer*>(buf)->position = 0;
}

/* ======================================================================
 * Tests
 * ====================================================================== */

TEST(BufferApi, AcquireReturnsNonNull) {
    netudp_buffer_t* buf = netudp_server_acquire_buffer(nullptr);
    ASSERT_NE(buf, nullptr);
    /* Release via send_buffer (nullptr server → error but pool slot freed) */
    netudp_server_send_buffer(nullptr, 0, 0, buf, 0);
}

TEST(BufferApi, WriteReadU8RoundTrip) {
    netudp_buffer_t* buf = netudp_server_acquire_buffer(nullptr);
    ASSERT_NE(buf, nullptr);

    netudp_buffer_write_u8(buf, 0x00);
    netudp_buffer_write_u8(buf, 0xAB);
    netudp_buffer_write_u8(buf, 0xFF);

    buf_rewind(buf);

    EXPECT_EQ(netudp_buffer_read_u8(buf), 0x00u);
    EXPECT_EQ(netudp_buffer_read_u8(buf), 0xABu);
    EXPECT_EQ(netudp_buffer_read_u8(buf), 0xFFu);

    netudp_server_send_buffer(nullptr, 0, 0, buf, 0);
}

TEST(BufferApi, WriteReadU16RoundTrip) {
    netudp_buffer_t* buf = netudp_server_acquire_buffer(nullptr);
    ASSERT_NE(buf, nullptr);

    netudp_buffer_write_u16(buf, 0xBEEFu);

    buf_rewind(buf);
    EXPECT_EQ(netudp_buffer_read_u16(buf), 0xBEEFu);

    netudp_server_send_buffer(nullptr, 0, 0, buf, 0);
}

TEST(BufferApi, WriteReadU32RoundTrip) {
    netudp_buffer_t* buf = netudp_server_acquire_buffer(nullptr);
    ASSERT_NE(buf, nullptr);

    netudp_buffer_write_u32(buf, 0xDEADBEEFu);

    buf_rewind(buf);
    EXPECT_EQ(netudp_buffer_read_u32(buf), 0xDEADBEEFu);

    netudp_server_send_buffer(nullptr, 0, 0, buf, 0);
}

TEST(BufferApi, WriteReadU64RoundTrip) {
    netudp_buffer_t* buf = netudp_server_acquire_buffer(nullptr);
    ASSERT_NE(buf, nullptr);

    netudp_buffer_write_u64(buf, 0x0102030405060708ULL);

    buf_rewind(buf);
    EXPECT_EQ(netudp_buffer_read_u64(buf), 0x0102030405060708ULL);

    netudp_server_send_buffer(nullptr, 0, 0, buf, 0);
}

TEST(BufferApi, WriteReadF32RoundTrip) {
    netudp_buffer_t* buf = netudp_server_acquire_buffer(nullptr);
    ASSERT_NE(buf, nullptr);

    netudp_buffer_write_f32(buf, 1.5F);

    buf_rewind(buf);
    EXPECT_FLOAT_EQ(netudp_buffer_read_f32(buf), 1.5F);

    netudp_server_send_buffer(nullptr, 0, 0, buf, 0);
}

TEST(BufferApi, WriteReadF64RoundTrip) {
    netudp_buffer_t* buf = netudp_server_acquire_buffer(nullptr);
    ASSERT_NE(buf, nullptr);

    netudp_buffer_write_f64(buf, 3.14159265358979);

    buf_rewind(buf);
    EXPECT_DOUBLE_EQ(netudp_buffer_read_f64(buf), 3.14159265358979);

    netudp_server_send_buffer(nullptr, 0, 0, buf, 0);
}

TEST(BufferApi, WriteReadVarintRoundTrip) {
    netudp_buffer_t* buf = netudp_server_acquire_buffer(nullptr);
    ASSERT_NE(buf, nullptr);

    netudp_buffer_write_varint(buf, 0);
    netudp_buffer_write_varint(buf, 1);
    netudp_buffer_write_varint(buf, 300);
    netudp_buffer_write_varint(buf, -1);
    netudp_buffer_write_varint(buf, 2147483647); /* INT_MAX */

    buf_rewind(buf);
    EXPECT_EQ(netudp_buffer_read_varint(buf), 0);
    EXPECT_EQ(netudp_buffer_read_varint(buf), 1);
    EXPECT_EQ(netudp_buffer_read_varint(buf), 300);
    EXPECT_EQ(netudp_buffer_read_varint(buf), -1);
    EXPECT_EQ(netudp_buffer_read_varint(buf), 2147483647);

    netudp_server_send_buffer(nullptr, 0, 0, buf, 0);
}

TEST(BufferApi, WriteReadBytesRoundTrip) {
    netudp_buffer_t* buf = netudp_server_acquire_buffer(nullptr);
    ASSERT_NE(buf, nullptr);

    const uint8_t src[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    netudp_buffer_write_bytes(buf, src, 8);

    buf_rewind(buf);

    uint8_t dst[8] = {};
    /* read_bytes is not in the public API — read u8 x8 instead */
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(netudp_buffer_read_u8(buf), src[i]);
    }

    netudp_server_send_buffer(nullptr, 0, 0, buf, 0);
}

TEST(BufferApi, WriteReadStringRoundTrip) {
    netudp_buffer_t* buf = netudp_server_acquire_buffer(nullptr);
    ASSERT_NE(buf, nullptr);

    const char msg[] = "hello";
    netudp_buffer_write_string(buf, msg, 16);

    buf_rewind(buf);

    /* string is stored as uint16 length + bytes */
    uint16_t len = netudp_buffer_read_u16(buf);
    EXPECT_EQ(len, static_cast<uint16_t>(5));
    char out[16] = {};
    for (int i = 0; i < 5; ++i) {
        out[i] = static_cast<char>(netudp_buffer_read_u8(buf));
    }
    EXPECT_EQ(std::memcmp(out, msg, 5), 0);

    netudp_server_send_buffer(nullptr, 0, 0, buf, 0);
}

TEST(BufferApi, ReadOnEmptyBufferReturnsZero) {
    netudp_buffer_t* buf = netudp_server_acquire_buffer(nullptr);
    ASSERT_NE(buf, nullptr);

    /* Seek to end by writing MTU bytes, then read — all overflow → 0 */
    for (int i = 0; i < NETUDP_MTU; ++i) {
        netudp_buffer_write_u8(buf, 0xEE);
    }
    /* Next write silently dropped (overflow) */
    netudp_buffer_write_u8(buf, 0xFF);

    EXPECT_EQ(netudp_buffer_read_u8(buf),    0u);
    EXPECT_EQ(netudp_buffer_read_u16(buf),   0u);
    EXPECT_EQ(netudp_buffer_read_u32(buf),   0u);
    EXPECT_EQ(netudp_buffer_read_u64(buf),   0u);
    EXPECT_FLOAT_EQ(netudp_buffer_read_f32(buf),  0.0F);
    EXPECT_DOUBLE_EQ(netudp_buffer_read_f64(buf), 0.0);
    EXPECT_EQ(netudp_buffer_read_varint(buf), 0);

    netudp_server_send_buffer(nullptr, 0, 0, buf, 0);
}

TEST(BufferApi, SendBufferNullServerReturnsParamError) {
    netudp_buffer_t* buf = netudp_server_acquire_buffer(nullptr);
    ASSERT_NE(buf, nullptr);
    netudp_buffer_write_u32(buf, 0xCAFEBABEu);

    int rc = netudp_server_send_buffer(nullptr, 0, 0, buf, NETUDP_SEND_RELIABLE);
    EXPECT_EQ(rc, NETUDP_ERROR_INVALID_PARAM);
    /* Buffer is returned to pool regardless of error */
}

TEST(BufferApi, PoolExhaustion) {
    /* The pool has 64 slots.  Exhaust it and verify acquire returns nullptr. */
    netudp_buffer_t* slots[64] = {};
    int acquired = 0;
    for (int i = 0; i < 64; ++i) {
        slots[i] = netudp_server_acquire_buffer(nullptr);
        if (slots[i] != nullptr) {
            ++acquired;
        }
    }
    EXPECT_EQ(acquired, 64);

    /* 65th acquire must fail */
    netudp_buffer_t* overflow = netudp_server_acquire_buffer(nullptr);
    EXPECT_EQ(overflow, nullptr);

    /* Release all */
    for (int i = 0; i < acquired; ++i) {
        netudp_server_send_buffer(nullptr, 0, 0, slots[i], 0);
    }

    /* Pool must be available again */
    netudp_buffer_t* again = netudp_server_acquire_buffer(nullptr);
    EXPECT_NE(again, nullptr);
    netudp_server_send_buffer(nullptr, 0, 0, again, 0);
}
