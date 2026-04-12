#include <gtest/gtest.h>
#include "../src/fragment/fragment.h"
#include "../src/wire/frame.h"

#include <cstring>
#include <vector>

/* ===== Fragment Header ===== */

TEST(Fragment, HeaderRoundTrip) {
    netudp::FragmentHeader hdr = {42, 3, 10};
    uint8_t buf[4] = {};
    netudp::write_fragment_header(hdr, buf);

    netudp::FragmentHeader read = netudp::read_fragment_header(buf);
    EXPECT_EQ(read.message_id, 42);
    EXPECT_EQ(read.fragment_index, 3);
    EXPECT_EQ(read.fragment_count, 10);
}

/* ===== Fragment Count Calculation ===== */

TEST(Fragment, CalcFragmentCount) {
    EXPECT_EQ(netudp::calc_fragment_count(100, 1150), 1);
    EXPECT_EQ(netudp::calc_fragment_count(1150, 1150), 1);
    EXPECT_EQ(netudp::calc_fragment_count(1151, 1150), 2);
    EXPECT_EQ(netudp::calc_fragment_count(65536, 1150), 57);
    EXPECT_EQ(netudp::calc_fragment_count(0, 1150), -1);
    EXPECT_EQ(netudp::calc_fragment_count(100, 0), -1);

    /* 256 fragments would exceed MAX_FRAGMENT_COUNT */
    EXPECT_EQ(netudp::calc_fragment_count(256 * 1150, 1150), -1);
}

/* ===== Fragment Tracker ===== */

TEST(Fragment, TrackerMarkAndComplete) {
    netudp::FragmentTracker tracker;
    tracker.total_fragments = 4;
    tracker.active = true;

    EXPECT_FALSE(tracker.is_complete());

    tracker.mark_fragment(0);
    tracker.mark_fragment(1);
    tracker.mark_fragment(2);
    EXPECT_FALSE(tracker.is_complete());
    EXPECT_EQ(tracker.next_missing(), 3);

    tracker.mark_fragment(3);
    EXPECT_TRUE(tracker.is_complete());
    EXPECT_EQ(tracker.next_missing(), -1);
}

TEST(Fragment, TrackerDuplicateFragment) {
    netudp::FragmentTracker tracker;
    tracker.total_fragments = 2;

    tracker.mark_fragment(0);
    EXPECT_EQ(tracker.received_count, 1);

    tracker.mark_fragment(0); /* Duplicate */
    EXPECT_EQ(tracker.received_count, 1); /* No change */
}

/* ===== Fragment Reassembler ===== */

TEST(Fragment, ReassembleSmallMessage) {
    netudp::FragmentReassembler reassembler;
    ASSERT_TRUE(reassembler.init(netudp::DEFAULT_MAX_MESSAGE_SIZE));

    int max_payload = 100;
    uint8_t frag0[100];
    std::memset(frag0, 0xAA, 100);
    uint8_t frag1[50];
    std::memset(frag1, 0xBB, 50);

    int out_size = 0;

    /* First fragment */
    auto* result = reassembler.on_fragment_received(1, 0, 2, frag0, 100, max_payload, 1.0, &out_size);
    EXPECT_EQ(result, nullptr); /* Not complete */

    /* Second fragment */
    result = reassembler.on_fragment_received(1, 1, 2, frag1, 50, max_payload, 1.001, &out_size);
    ASSERT_NE(result, nullptr); /* Complete! */
    EXPECT_EQ(out_size, 150);

    /* Verify data */
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(result[i], 0xAA);
    }
    for (int i = 100; i < 150; ++i) {
        EXPECT_EQ(result[i], 0xBB);
    }
}

TEST(Fragment, ReassembleOutOfOrder) {
    netudp::FragmentReassembler reassembler;
    ASSERT_TRUE(reassembler.init(netudp::DEFAULT_MAX_MESSAGE_SIZE));

    int max_payload = 50;
    uint8_t frag[50];
    int out_size = 0;

    /* Receive fragment 2 before 0 and 1 */
    std::memset(frag, 2, 50);
    EXPECT_EQ(reassembler.on_fragment_received(1, 2, 3, frag, 50, max_payload, 1.0, &out_size), nullptr);

    std::memset(frag, 0, 50);
    EXPECT_EQ(reassembler.on_fragment_received(1, 0, 3, frag, 50, max_payload, 1.001, &out_size), nullptr);

    std::memset(frag, 1, 50);
    auto* result = reassembler.on_fragment_received(1, 1, 3, frag, 50, max_payload, 1.002, &out_size);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(out_size, 150);
}

TEST(Fragment, DuplicateFragmentIgnored) {
    netudp::FragmentReassembler reassembler;
    ASSERT_TRUE(reassembler.init(netudp::DEFAULT_MAX_MESSAGE_SIZE));

    uint8_t frag[50] = {};
    int out_size = 0;

    reassembler.on_fragment_received(1, 0, 2, frag, 50, 50, 1.0, &out_size);
    auto* result = reassembler.on_fragment_received(1, 0, 2, frag, 50, 50, 1.0, &out_size);
    EXPECT_EQ(result, nullptr); /* Duplicate, ignored */
}

TEST(Fragment, Timeout) {
    netudp::FragmentReassembler reassembler;
    ASSERT_TRUE(reassembler.init(netudp::DEFAULT_MAX_MESSAGE_SIZE));

    uint8_t frag[50] = {};
    int out_size = 0;

    /* Start a reassembly but never complete it */
    reassembler.on_fragment_received(1, 0, 3, frag, 50, 50, 1.0, &out_size);

    /* After timeout */
    int cleaned = reassembler.cleanup_timeout(1.0 + netudp::FRAGMENT_TIMEOUT_SEC + 0.1);
    EXPECT_EQ(cleaned, 1);
}

TEST(Fragment, InvalidParams) {
    netudp::FragmentReassembler reassembler;
    ASSERT_TRUE(reassembler.init(netudp::DEFAULT_MAX_MESSAGE_SIZE));

    int out_size = 0;
    uint8_t frag[50] = {};

    /* fragment_count = 0 */
    EXPECT_EQ(reassembler.on_fragment_received(1, 0, 0, frag, 50, 50, 1.0, &out_size), nullptr);

    /* fragment_index >= fragment_count */
    EXPECT_EQ(reassembler.on_fragment_received(1, 3, 3, frag, 50, 50, 1.0, &out_size), nullptr);
}

/* ===== Wire Frame Tests ===== */

TEST(WireFrame, UnreliableFrame) {
    uint8_t buf[256] = {};
    uint8_t data[] = {1, 2, 3, 4, 5};

    int written = netudp::wire::write_unreliable_frame(buf, 256, 0, data, 5);
    EXPECT_EQ(written, 9); /* type(1) + channel(1) + len(2) + data(5) */
    EXPECT_EQ(buf[0], netudp::wire::FRAME_UNRELIABLE_DATA);
    EXPECT_EQ(buf[1], 0); /* channel */

    uint16_t len = 0;
    std::memcpy(&len, buf + 2, 2);
    EXPECT_EQ(len, 5);
    EXPECT_EQ(buf[4], 1);
}

TEST(WireFrame, ReliableFrame) {
    uint8_t buf[256] = {};
    uint8_t data[] = {0xAA, 0xBB};

    int written = netudp::wire::write_reliable_frame(buf, 256, 2, 42, data, 2);
    EXPECT_EQ(written, 8); /* type(1) + channel(1) + seq(2) + len(2) + data(2) */
    EXPECT_EQ(buf[0], netudp::wire::FRAME_RELIABLE_DATA);
    EXPECT_EQ(buf[1], 2); /* channel */

    uint16_t seq = 0;
    std::memcpy(&seq, buf + 2, 2);
    EXPECT_EQ(seq, 42);
}

TEST(WireFrame, FragmentFrame) {
    uint8_t buf[256] = {};
    uint8_t data[] = {0xFF};

    int written = netudp::wire::write_fragment_frame(buf, 256, 1, 100, 3, 10, data, 1);
    EXPECT_EQ(written, 7); /* type(1) + channel(1) + msg_id(2) + idx(1) + cnt(1) + data(1) */
    EXPECT_EQ(buf[0], netudp::wire::FRAME_FRAGMENT_DATA);
    EXPECT_EQ(buf[4], 3);  /* frag_idx */
    EXPECT_EQ(buf[5], 10); /* frag_cnt */
}

TEST(WireFrame, DisconnectFrame) {
    uint8_t buf[4] = {};
    int written = netudp::wire::write_disconnect_frame(buf, 4, 42);
    EXPECT_EQ(written, 2);
    EXPECT_EQ(buf[0], netudp::wire::FRAME_DISCONNECT);
    EXPECT_EQ(buf[1], 42);
}

TEST(WireFrame, BufferTooSmall) {
    uint8_t buf[2] = {};
    uint8_t data[100] = {};
    EXPECT_EQ(netudp::wire::write_unreliable_frame(buf, 2, 0, data, 100), -1);
    EXPECT_EQ(netudp::wire::write_reliable_frame(buf, 2, 0, 0, data, 100), -1);
    EXPECT_EQ(netudp::wire::write_fragment_frame(buf, 2, 0, 0, 0, 0, data, 100), -1);
    EXPECT_EQ(netudp::wire::write_disconnect_frame(buf, 1, 0), -1);
}

TEST(WireFrame, PeekFrameType) {
    uint8_t buf[] = {netudp::wire::FRAME_RELIABLE_DATA, 0, 0};
    EXPECT_EQ(netudp::wire::peek_frame_type(buf, 3), netudp::wire::FRAME_RELIABLE_DATA);
    EXPECT_EQ(netudp::wire::peek_frame_type(buf, 0), 0); /* Empty */
}
