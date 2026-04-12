#include <gtest/gtest.h>
#include <netudp/netudp.h>
#include "../src/channel/channel.h"
#include "../src/reliability/reliable_channel.h"

#include <cstring>
#include <vector>

/* ===== Channel Tests ===== */

TEST(Channel, QueueAndDequeue) {
    netudp::Channel ch;
    netudp_channel_config_t cfg = {};
    cfg.type = NETUDP_CHANNEL_UNRELIABLE;
    cfg.nagle_ms = 0; /* No Nagle */
    ch.init(0, cfg);

    uint8_t data[] = {1, 2, 3, 4};
    EXPECT_TRUE(ch.queue_send(data, 4, 0));
    EXPECT_EQ(ch.send_queue_size(), 1);
    EXPECT_TRUE(ch.has_pending(1.0));

    netudp::QueuedMessage msg;
    EXPECT_TRUE(ch.dequeue_send(&msg));
    EXPECT_EQ(msg.size, 4);
    EXPECT_EQ(msg.data[0], 1);
    EXPECT_EQ(ch.send_queue_size(), 0);
}

TEST(Channel, NagleBatching) {
    netudp::Channel ch;
    netudp_channel_config_t cfg = {};
    cfg.type = NETUDP_CHANNEL_RELIABLE_ORDERED;
    cfg.nagle_ms = 50; /* 50ms Nagle */
    ch.init(0, cfg);

    uint8_t data[] = {1};
    ch.queue_send(data, 1, 0);
    ch.start_nagle(1.0);

    /* Not ready yet (< 50ms) */
    EXPECT_FALSE(ch.has_pending(1.020));

    /* Ready after 50ms */
    EXPECT_TRUE(ch.has_pending(1.060));
}

TEST(Channel, NoNagleBypassesTimer) {
    netudp::Channel ch;
    netudp_channel_config_t cfg = {};
    cfg.type = NETUDP_CHANNEL_RELIABLE_ORDERED;
    cfg.nagle_ms = 100;
    ch.init(0, cfg);

    uint8_t data[] = {1};
    ch.queue_send(data, 1, NETUDP_SEND_NO_NAGLE);
    ch.start_nagle(1.0);

    EXPECT_TRUE(ch.has_pending(1.0)); /* Immediate due to NO_NAGLE */
}

TEST(Channel, FlushBypasses) {
    netudp::Channel ch;
    netudp_channel_config_t cfg = {};
    cfg.type = NETUDP_CHANNEL_UNRELIABLE;
    cfg.nagle_ms = 1000;
    ch.init(0, cfg);

    uint8_t data[] = {1};
    ch.queue_send(data, 1, 0);
    ch.start_nagle(1.0);

    EXPECT_FALSE(ch.has_pending(1.001)); /* Nagle active */
    ch.flush();
    EXPECT_TRUE(ch.has_pending(1.001)); /* Flushed */
}

TEST(Channel, UnreliableSequencedDropsStale) {
    netudp::Channel ch;
    netudp_channel_config_t cfg = {};
    cfg.type = NETUDP_CHANNEL_UNRELIABLE_SEQUENCED;
    ch.init(0, cfg);

    EXPECT_TRUE(ch.on_recv_unreliable_sequenced(1));
    EXPECT_TRUE(ch.on_recv_unreliable_sequenced(3)); /* Skip 2 */
    EXPECT_FALSE(ch.on_recv_unreliable_sequenced(2)); /* Stale! */
    EXPECT_TRUE(ch.on_recv_unreliable_sequenced(4));
}

TEST(Channel, PriorityScheduler) {
    netudp::Channel channels[3];

    netudp_channel_config_t cfg0 = {};
    cfg0.type = NETUDP_CHANNEL_UNRELIABLE;
    cfg0.priority = 10;
    cfg0.nagle_ms = 0;
    channels[0].init(0, cfg0);

    netudp_channel_config_t cfg1 = {};
    cfg1.type = NETUDP_CHANNEL_RELIABLE_ORDERED;
    cfg1.priority = 50; /* Higher priority */
    cfg1.nagle_ms = 0;
    channels[1].init(1, cfg1);

    netudp_channel_config_t cfg2 = {};
    cfg2.type = NETUDP_CHANNEL_UNRELIABLE;
    cfg2.priority = 10;
    cfg2.nagle_ms = 0;
    channels[2].init(2, cfg2);

    uint8_t data[] = {1};
    channels[0].queue_send(data, 1, 0);
    channels[1].queue_send(data, 1, 0);
    channels[2].queue_send(data, 1, 0);

    /* Scheduler should pick channel 1 (highest priority) */
    int next = netudp::ChannelScheduler::next_channel(channels, 3, 1.0);
    EXPECT_EQ(next, 1);
}

TEST(Channel, QueueFull) {
    netudp::Channel ch;
    netudp_channel_config_t cfg = {};
    cfg.nagle_ms = 0;
    ch.init(0, cfg);

    uint8_t data[] = {1};
    for (int i = 0; i < 256; ++i) {
        EXPECT_TRUE(ch.queue_send(data, 1, 0));
    }
    EXPECT_FALSE(ch.queue_send(data, 1, 0)); /* Full */
}

TEST(Channel, OversizedMessageRejected) {
    netudp::Channel ch;
    netudp_channel_config_t cfg = {};
    ch.init(0, cfg);

    uint8_t big[NETUDP_MTU + 1] = {};
    EXPECT_FALSE(ch.queue_send(big, NETUDP_MTU + 1, 0));
}

TEST(Channel, EmptyMessageRejected) {
    netudp::Channel ch;
    netudp_channel_config_t cfg = {};
    ch.init(0, cfg);

    EXPECT_FALSE(ch.queue_send(nullptr, 0, 0));
}

TEST(Channel, SchedulerNoPending) {
    netudp::Channel channels[2];
    netudp_channel_config_t cfg = {};
    cfg.nagle_ms = 0;
    channels[0].init(0, cfg);
    channels[1].init(1, cfg);

    EXPECT_EQ(netudp::ChannelScheduler::next_channel(channels, 2, 1.0), -1);
}

/* ===== ReliableChannelState Tests ===== */

TEST(ReliableChannel, SendAndAck) {
    netudp::ReliableChannelState state;

    uint8_t data[] = {10, 20, 30};
    state.record_send(data, 3, 0, 1.0);
    state.record_send(data, 3, 1, 1.016);

    EXPECT_EQ(state.send_seq, 2);
    EXPECT_EQ(state.oldest_unacked, 0);

    state.mark_acked(0);
    EXPECT_EQ(state.oldest_unacked, 1);

    state.mark_acked(1);
    EXPECT_EQ(state.oldest_unacked, 2);
}

TEST(ReliableChannel, OrderedDelivery) {
    netudp::ReliableChannelState state;

    /* Receive messages out of order: 0, 2, 1 */
    uint8_t d0[] = {0xAA};
    uint8_t d1[] = {0xBB};
    uint8_t d2[] = {0xCC};

    state.buffer_received_ordered(0, d0, 1);
    state.buffer_received_ordered(2, d2, 1); /* Out of order */

    /* Deliver: should get 0 only (1 is missing) */
    std::vector<uint8_t> delivered;
    state.deliver_ordered([&](const uint8_t* data, int len, uint16_t /*seq*/) {
        delivered.push_back(data[0]);
        (void)len;
    });
    ASSERT_EQ(delivered.size(), 1U);
    EXPECT_EQ(delivered[0], 0xAA);
    EXPECT_EQ(state.recv_seq, 1);

    /* Now receive 1 */
    state.buffer_received_ordered(1, d1, 1);

    delivered.clear();
    state.deliver_ordered([&](const uint8_t* data, int len, uint16_t /*seq*/) {
        delivered.push_back(data[0]);
        (void)len;
    });
    ASSERT_EQ(delivered.size(), 2U); /* 1 and 2 delivered together */
    EXPECT_EQ(delivered[0], 0xBB);
    EXPECT_EQ(delivered[1], 0xCC);
    EXPECT_EQ(state.recv_seq, 3);
}

TEST(ReliableChannel, OrderedDuplicateRejected) {
    netudp::ReliableChannelState state;

    uint8_t data[] = {1};
    EXPECT_TRUE(state.buffer_received_ordered(0, data, 1));
    EXPECT_FALSE(state.buffer_received_ordered(0, data, 1)); /* Duplicate */
}

TEST(ReliableChannel, OrderedOldRejected) {
    netudp::ReliableChannelState state;

    uint8_t data[] = {1};
    state.buffer_received_ordered(0, data, 1);
    state.deliver_ordered([](const uint8_t*, int, uint16_t) {});
    EXPECT_EQ(state.recv_seq, 1);

    /* Seq 0 already delivered */
    EXPECT_FALSE(state.buffer_received_ordered(0, data, 1));
}

TEST(ReliableChannel, UnorderedDelivery) {
    netudp::ReliableChannelState state;

    /* Receive 2 before 1 — both should be deliverable immediately */
    EXPECT_FALSE(state.is_received_unordered(0));
    state.mark_received_unordered(0);
    EXPECT_TRUE(state.is_received_unordered(0));

    /* Duplicate */
    state.mark_received_unordered(0);
    EXPECT_TRUE(state.is_received_unordered(0));
}

TEST(ReliableChannel, RetransmitTimeout) {
    netudp::ReliableChannelState state;

    uint8_t data[] = {1, 2, 3};
    state.record_send(data, 3, 0, 1.0);
    state.record_send(data, 3, 1, 1.0);

    /* After RTO (1.0s), both should need retransmit */
    uint16_t retransmit_seqs[16];
    int count = state.find_retransmits(2.1, 1.0, retransmit_seqs, 16);
    EXPECT_EQ(count, 2);

    /* Get message for retransmit */
    auto* msg = state.get_for_retransmit(0);
    ASSERT_NE(msg, nullptr);
    EXPECT_EQ(msg->data_len, 3);
    msg->retry_count++;
    msg->send_time = 2.1;
}

TEST(ReliableChannel, MaxRetriesDropsMessage) {
    netudp::ReliableChannelState state;

    uint8_t data[] = {1};
    state.record_send(data, 1, 0, 0.0);

    /* Set retry count to MAX */
    auto* msg = state.get_for_retransmit(0);
    ASSERT_NE(msg, nullptr);
    msg->retry_count = netudp::MAX_RETRIES;

    /* Should be dropped, not returned for retransmit */
    uint16_t seqs[16];
    int count = state.find_retransmits(100.0, 1.0, seqs, 16);
    EXPECT_EQ(count, 0);
    EXPECT_EQ(state.messages_dropped(), 1U);
}

TEST(ReliableChannel, Reset) {
    netudp::ReliableChannelState state;

    uint8_t data[] = {1};
    state.record_send(data, 1, 0, 1.0);
    state.buffer_received_ordered(0, data, 1);

    state.reset();
    EXPECT_EQ(state.send_seq, 0);
    EXPECT_EQ(state.recv_seq, 0);
    EXPECT_EQ(state.oldest_unacked, 0);
}

TEST(ReliableChannel, RecordSendOversized) {
    netudp::ReliableChannelState state;
    uint8_t data[1] = {};
    EXPECT_FALSE(state.record_send(data, 0, 0, 0.0));
    EXPECT_FALSE(state.record_send(data, 1201, 0, 0.0));
}

TEST(ReliableChannel, BufferTooFarAhead) {
    netudp::ReliableChannelState state;
    uint8_t data[] = {1};
    EXPECT_FALSE(state.buffer_received_ordered(netudp::RELIABLE_BUFFER_SIZE, data, 1));
}
