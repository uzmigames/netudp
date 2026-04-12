#include <gtest/gtest.h>
#include "../src/reliability/packet_tracker.h"
#include "../src/reliability/rtt.h"

/* ===== PacketTracker Tests ===== */

TEST(PacketTracker, SequentialSendAndAck) {
    netudp::PacketTracker tracker;

    /* Send 5 packets */
    for (int i = 0; i < 5; ++i) {
        uint16_t seq = tracker.send_packet(1.0 + i * 0.016);
        EXPECT_EQ(seq, static_cast<uint16_t>(i));
    }
    EXPECT_EQ(tracker.send_sequence(), 5);

    /* Simulate remote receiving all 5, then acking with ack=4, ack_bits covers 0-3 */
    netudp::AckFields ack = {};
    ack.ack = 4;
    ack.ack_bits = 0x0F; /* bits 0-3: packets 3,2,1,0 received */
    ack.ack_delay_us = 100;

    int newly_acked = tracker.process_acks(ack);
    EXPECT_EQ(newly_acked, 5);

    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(tracker.is_acked(static_cast<uint16_t>(i)));
    }
}

TEST(PacketTracker, OutOfOrderAck) {
    netudp::PacketTracker tracker;

    for (int i = 0; i < 5; ++i) {
        tracker.send_packet(1.0);
    }

    /* Remote only received packets 0, 2, 4 (missed 1, 3) */
    netudp::AckFields ack = {};
    ack.ack = 4;
    ack.ack_bits = 0x05; /* bit 0: seq 3 received? no. bit 1: seq 2 received. bit 0: seq 3... */
    /* Actually: bit 0 = ack-1-0 = 3, bit 1 = ack-1-1 = 2, bit 2 = ack-2-1 = 1, etc. */
    /* ack_bits bit N = received (ack - N - 1) */
    /* For received 0,2,4: ack=4, need bits for 3(=ack-0-1 → bit0), 2(=ack-1-1 → bit1), 1(=ack-2-1 → bit2), 0(=ack-3-1 → bit3) */
    /* received: 0(bit3),2(bit1),4(ack) → ack_bits = 0b1010 = 0x0A */
    ack.ack_bits = 0x0A; /* 0,2 received; 1,3 missed */

    int newly_acked = tracker.process_acks(ack);
    EXPECT_EQ(newly_acked, 3); /* 4, 2, 0 */

    EXPECT_TRUE(tracker.is_acked(0));
    EXPECT_FALSE(tracker.is_acked(1));
    EXPECT_TRUE(tracker.is_acked(2));
    EXPECT_FALSE(tracker.is_acked(3));
    EXPECT_TRUE(tracker.is_acked(4));
}

TEST(PacketTracker, WindowFull) {
    netudp::PacketTracker tracker;

    for (int i = 0; i < netudp::PACKET_WINDOW_SIZE; ++i) {
        EXPECT_FALSE(tracker.is_window_full());
        tracker.send_packet(1.0);
    }

    EXPECT_TRUE(tracker.is_window_full());
}

TEST(PacketTracker, WindowAdvancesOnAck) {
    netudp::PacketTracker tracker;

    for (int i = 0; i < netudp::PACKET_WINDOW_SIZE; ++i) {
        tracker.send_packet(1.0);
    }
    EXPECT_TRUE(tracker.is_window_full());

    /* Ack first packet */
    netudp::AckFields ack = {};
    ack.ack = 0;
    tracker.process_acks(ack);

    EXPECT_FALSE(tracker.is_window_full());
}

TEST(PacketTracker, AckFieldsSerialization) {
    netudp::AckFields original = {};
    original.ack = 42;
    original.ack_bits = 0xDEADBEEF;
    original.ack_delay_us = 1234;

    uint8_t buf[8] = {};
    netudp::write_ack_fields(original, buf);

    netudp::AckFields read = netudp::read_ack_fields(buf);
    EXPECT_EQ(read.ack, 42);
    EXPECT_EQ(read.ack_bits, 0xDEADBEEFU);
    EXPECT_EQ(read.ack_delay_us, 1234);
}

TEST(PacketTracker, ReceiveAndBuildAck) {
    netudp::PacketTracker tracker;

    /* Receive packets 0, 1, 2 */
    tracker.on_packet_received(0, 1.000);
    tracker.on_packet_received(1, 1.001);
    tracker.on_packet_received(2, 1.002);

    netudp::AckFields ack = tracker.build_ack_fields(1.003);
    EXPECT_EQ(ack.ack, 2);
    /* ack_bits: bit 0 = seq 1 received, bit 1 = seq 0 received */
    EXPECT_NE(ack.ack_bits & 0x01, 0U); /* seq 1 */
    EXPECT_NE(ack.ack_bits & 0x02, 0U); /* seq 0 */
    EXPECT_GT(ack.ack_delay_us, 0);
}

TEST(PacketTracker, ReceiveOutOfOrder) {
    netudp::PacketTracker tracker;

    tracker.on_packet_received(0, 1.0);
    tracker.on_packet_received(3, 1.001); /* Skip 1, 2 */
    tracker.on_packet_received(1, 1.002); /* Late arrival */

    netudp::AckFields ack = tracker.build_ack_fields(1.003);
    EXPECT_EQ(ack.ack, 3);
    /* bit 0 = seq 2 (not received), bit 1 = seq 1 (received), bit 2 = seq 0 (received) */
    EXPECT_EQ(ack.ack_bits & 0x01, 0U); /* seq 2 NOT received */
    EXPECT_NE(ack.ack_bits & 0x02, 0U); /* seq 1 received */
    EXPECT_NE(ack.ack_bits & 0x04, 0U); /* seq 0 received */
}

TEST(PacketTracker, GetSendTime) {
    netudp::PacketTracker tracker;
    tracker.send_packet(1.5);
    tracker.send_packet(2.0);
    EXPECT_DOUBLE_EQ(tracker.get_send_time(0), 1.5);
    EXPECT_DOUBLE_EQ(tracker.get_send_time(1), 2.0);
    EXPECT_DOUBLE_EQ(tracker.get_send_time(99), -1.0); /* Not found */
}

TEST(PacketTracker, Reset) {
    netudp::PacketTracker tracker;
    tracker.send_packet(1.0);
    tracker.on_packet_received(0, 1.0);
    tracker.reset();
    EXPECT_EQ(tracker.send_sequence(), 0);
    EXPECT_EQ(tracker.oldest_unacked(), 0);
}

/* ===== RTT Estimator Tests ===== */

TEST(RttEstimator, FirstSample) {
    netudp::RttEstimator rtt;
    EXPECT_FALSE(rtt.has_samples());
    EXPECT_DOUBLE_EQ(rtt.rto(), 1.0); /* Initial */

    rtt.on_sample(1.0, 1.050, 0); /* 50ms RTT */
    EXPECT_TRUE(rtt.has_samples());
    EXPECT_NEAR(rtt.srtt(), 0.050, 0.001);
    EXPECT_NEAR(rtt.rttvar(), 0.025, 0.001);
}

TEST(RttEstimator, ConvergesOnStable) {
    netudp::RttEstimator rtt;

    /* Feed 20 samples of ~50ms RTT */
    for (int i = 0; i < 20; ++i) {
        rtt.on_sample(0.0, 0.050, 0);
    }

    EXPECT_NEAR(rtt.srtt(), 0.050, 0.005);
    EXPECT_LT(rtt.rttvar(), 0.010);
}

TEST(RttEstimator, RTOClampedToMin) {
    netudp::RttEstimator rtt;

    /* Very fast RTT — RTO should be clamped to MIN_RTO */
    for (int i = 0; i < 20; ++i) {
        rtt.on_sample(0.0, 0.001, 0); /* 1ms */
    }

    EXPECT_GE(rtt.rto(), netudp::RttEstimator::MIN_RTO);
}

TEST(RttEstimator, RTOClampedToMax) {
    netudp::RttEstimator rtt;

    /* Very slow RTT with high variance */
    rtt.on_sample(0.0, 1.5, 0);
    rtt.on_sample(0.0, 0.5, 0);

    EXPECT_LE(rtt.rto(), netudp::RttEstimator::MAX_RTO);
}

TEST(RttEstimator, AckDelaySubtracted) {
    netudp::RttEstimator rtt;

    /* 100ms total, 20ms ack delay → 80ms RTT */
    rtt.on_sample(1.0, 1.100, 20000);
    EXPECT_NEAR(rtt.srtt(), 0.080, 0.005);
}

TEST(RttEstimator, PingMs) {
    netudp::RttEstimator rtt;
    rtt.on_sample(0.0, 0.050, 0);
    EXPECT_EQ(rtt.ping_ms(), 50U);
}

TEST(RttEstimator, Reset) {
    netudp::RttEstimator rtt;
    rtt.on_sample(0.0, 0.050, 0);
    rtt.reset();
    EXPECT_FALSE(rtt.has_samples());
    EXPECT_DOUBLE_EQ(rtt.rto(), 1.0);
}
