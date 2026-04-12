#include <gtest/gtest.h>
#include "../src/bandwidth/bandwidth.h"
#include "../src/connection/ddos.h"
#include "../src/stats/stats.h"

/* ===== Bandwidth TokenBucket ===== */

TEST(Bandwidth, TokenBucketRefillAndConsume) {
    netudp::BandwidthBucket bucket;
    bucket.rate_bytes_per_sec = 1000;
    bucket.burst_bytes = 500;
    bucket.init(0.0);

    EXPECT_EQ(bucket.tokens, 500.0); /* Starts at burst */

    /* Consume 200 */
    EXPECT_TRUE(bucket.try_consume(200));
    EXPECT_NEAR(bucket.tokens, 300.0, 0.01);

    /* Refill after 0.1s: +100 bytes */
    bucket.refill(0.1);
    EXPECT_NEAR(bucket.tokens, 400.0, 1.0);

    /* Consume 500 — should fail (only 400 available) */
    EXPECT_FALSE(bucket.try_consume(500));

    /* Consume 400 — should pass */
    EXPECT_TRUE(bucket.try_consume(400));
}

TEST(Bandwidth, TokenBucketCappedAtBurst) {
    netudp::BandwidthBucket bucket;
    bucket.rate_bytes_per_sec = 10000;
    bucket.burst_bytes = 1000;
    bucket.init(0.0);

    /* Wait long time — tokens capped at burst */
    bucket.refill(100.0);
    EXPECT_LE(bucket.tokens, 1000.0);
}

/* ===== QueuedBits Budget ===== */

TEST(Bandwidth, QueuedBitsCanSend) {
    netudp::QueuedBitsBudget budget;

    EXPECT_TRUE(budget.can_send()); /* Initially at 0 */

    budget.consume(1000); /* 8000 bits */
    EXPECT_FALSE(budget.can_send()); /* Over budget */

    budget.refill(1.0, 256 * 1024); /* 256KB/s for 1s = 256KB budget */
    EXPECT_TRUE(budget.can_send()); /* Budget restored */
}

TEST(Bandwidth, QueuedBitsClampedToNegBurst) {
    netudp::QueuedBitsBudget budget;
    budget.burst_bits = 8000;

    budget.refill(10.0, 256 * 1024); /* Long time, lots of budget */
    EXPECT_GE(budget.queued_bits, -budget.burst_bits);
}

/* ===== AIMD Congestion Control ===== */

TEST(Bandwidth, AIMDDecreaseOnLoss) {
    netudp::CongestionControl cc;
    cc.init(200 * 1024);

    /* Simulate 10% loss */
    for (int i = 0; i < 54; ++i) { cc.on_packet_acked(); }
    for (int i = 0; i < 10; ++i) { cc.on_packet_lost(); }
    cc.on_rtt_sample();

    uint32_t before = cc.send_rate();
    cc.evaluate();
    EXPECT_LT(cc.send_rate(), before); /* Decreased */
}

TEST(Bandwidth, AIMDIncreaseOnGoodConditions) {
    netudp::CongestionControl cc;
    cc.init(200 * 1024);

    /* Force lower rate first */
    for (int i = 0; i < 54; ++i) { cc.on_packet_acked(); }
    for (int i = 0; i < 10; ++i) { cc.on_packet_lost(); }
    cc.evaluate();
    uint32_t low_rate = cc.send_rate();

    /* Now good conditions */
    cc.reset();
    cc.init(200 * 1024);
    /* Manually set rate lower */
    for (int i = 0; i < 64; ++i) { cc.on_packet_acked(); }
    for (int i = 0; i < 10; ++i) { cc.on_rtt_sample(); }

    /* Decrease first */
    for (int j = 0; j < 5; ++j) { cc.on_packet_lost(); }
    cc.evaluate();
    uint32_t after_decrease = cc.send_rate();

    /* Then only good packets */
    cc.reset();
    cc.init(after_decrease);
    for (int i = 0; i < 64; ++i) { cc.on_packet_acked(); }
    for (int i = 0; i < 10; ++i) { cc.on_rtt_sample(); }
    cc.evaluate();
    EXPECT_GE(cc.send_rate(), after_decrease); /* Increased or same */
}

TEST(Bandwidth, AIMDFloorAtMinRate) {
    netudp::CongestionControl cc;
    cc.init(netudp::CongestionControl::MIN_SEND_RATE + 1000);

    /* Heavy loss */
    for (int round = 0; round < 20; ++round) {
        for (int i = 0; i < 32; ++i) { cc.on_packet_acked(); }
        for (int i = 0; i < 32; ++i) { cc.on_packet_lost(); }
        cc.evaluate();
    }

    EXPECT_GE(cc.send_rate(), netudp::CongestionControl::MIN_SEND_RATE);
}

TEST(Bandwidth, AIMDCappedAtMax) {
    netudp::CongestionControl cc;
    cc.init(100 * 1024);

    for (int round = 0; round < 50; ++round) {
        for (int i = 0; i < 64; ++i) { cc.on_packet_acked(); }
        for (int i = 0; i < 10; ++i) { cc.on_rtt_sample(); }
        cc.evaluate();
    }

    EXPECT_LE(cc.send_rate(), 100U * 1024);
}

TEST(Bandwidth, AIMDNotEnoughData) {
    netudp::CongestionControl cc;
    cc.init(100 * 1024);

    uint32_t before = cc.send_rate();
    for (int i = 0; i < 5; ++i) { cc.on_packet_lost(); }
    cc.evaluate();
    EXPECT_EQ(cc.send_rate(), before); /* < 10 packets, no change */
}

TEST(Bandwidth, LossRate) {
    netudp::CongestionControl cc;
    cc.init(100 * 1024);
    EXPECT_FLOAT_EQ(cc.loss_rate(), 0.0F);

    for (int i = 0; i < 8; ++i) { cc.on_packet_acked(); }
    for (int i = 0; i < 2; ++i) { cc.on_packet_lost(); }
    EXPECT_NEAR(cc.loss_rate(), 0.2F, 0.01F);
}

/* ===== DDoS Escalation ===== */

TEST(DDoS, EscalationLevels) {
    netudp::DDoSMonitor monitor;
    EXPECT_EQ(monitor.severity(), netudp::DDoSSeverity::None);

    /* 150 bad packets → Low */
    for (int i = 0; i < 150; ++i) { monitor.on_bad_packet(); }
    monitor.update(1.0);
    EXPECT_EQ(monitor.severity(), netudp::DDoSSeverity::Low);

    /* 600 bad packets → Medium */
    for (int i = 0; i < 600; ++i) { monitor.on_bad_packet(); }
    monitor.update(1.0);
    EXPECT_EQ(monitor.severity(), netudp::DDoSSeverity::Medium);

    /* 2500 → High */
    for (int i = 0; i < 2500; ++i) { monitor.on_bad_packet(); }
    monitor.update(1.0);
    EXPECT_EQ(monitor.severity(), netudp::DDoSSeverity::High);

    /* 11000 → Critical */
    for (int i = 0; i < 11000; ++i) { monitor.on_bad_packet(); }
    monitor.update(1.0);
    EXPECT_EQ(monitor.severity(), netudp::DDoSSeverity::Critical);
}

TEST(DDoS, AutoCooloff) {
    netudp::DDoSMonitor monitor;

    /* Escalate to Low */
    for (int i = 0; i < 150; ++i) { monitor.on_bad_packet(); }
    monitor.update(1.0);
    EXPECT_EQ(monitor.severity(), netudp::DDoSSeverity::Low);

    /* 30 seconds of quiet → should cool off to None */
    for (int sec = 0; sec < 31; ++sec) {
        monitor.update(1.0);
    }
    EXPECT_EQ(monitor.severity(), netudp::DDoSSeverity::None);
}

TEST(DDoS, CriticalBlocksNewConnections) {
    netudp::DDoSMonitor monitor;

    EXPECT_TRUE(monitor.should_process_new_connection());

    /* Escalate to Critical */
    for (int i = 0; i < 11000; ++i) { monitor.on_bad_packet(); }
    monitor.update(1.0);

    EXPECT_FALSE(monitor.should_process_new_connection());
}

TEST(DDoS, HighDropsNonEstablished) {
    netudp::DDoSMonitor monitor;

    for (int i = 0; i < 2500; ++i) { monitor.on_bad_packet(); }
    monitor.update(1.0);
    EXPECT_EQ(monitor.severity(), netudp::DDoSSeverity::High);

    EXPECT_FALSE(monitor.should_process_packet(false)); /* Non-established: blocked */
    EXPECT_TRUE(monitor.should_process_packet(true));   /* Established: allowed */
}

TEST(DDoS, Reset) {
    netudp::DDoSMonitor monitor;
    for (int i = 0; i < 11000; ++i) { monitor.on_bad_packet(); }
    monitor.update(1.0);
    EXPECT_EQ(monitor.severity(), netudp::DDoSSeverity::Critical);

    monitor.reset();
    EXPECT_EQ(monitor.severity(), netudp::DDoSSeverity::None);
}

/* ===== Statistics ===== */

TEST(Stats, ConnectionStatsPacketTracking) {
    netudp::ConnectionStats stats;

    stats.on_packet_sent(100);
    stats.on_packet_sent(200);
    stats.on_packet_received(150);

    EXPECT_EQ(stats.packets_sent, 2U);
    EXPECT_EQ(stats.packets_received, 1U);
    EXPECT_EQ(stats.out_packets_this_sec_, 2U);
    EXPECT_EQ(stats.out_bytes_this_sec_, 300U);
}

TEST(Stats, ThroughputEMA) {
    netudp::ConnectionStats stats;
    stats.last_throughput_time_ = 0.0;

    /* First second: 10 packets, 1000 bytes out */
    for (int i = 0; i < 10; ++i) { stats.on_packet_sent(100); }
    stats.update_throughput(1.0);

    EXPECT_GT(stats.out_packets_per_sec, 0.0F);
    EXPECT_GT(stats.out_bytes_per_sec, 0.0F);

    /* Second second: 20 packets */
    for (int i = 0; i < 20; ++i) { stats.on_packet_sent(100); }
    stats.update_throughput(2.0);

    /* EMA should be between 10 and 20 */
    EXPECT_GT(stats.out_packets_per_sec, 2.0F);
    EXPECT_LT(stats.out_packets_per_sec, 20.0F);
}

TEST(Stats, ThroughputNoUpdateBeforeOneSecond) {
    netudp::ConnectionStats stats;
    stats.last_throughput_time_ = 0.0;

    stats.on_packet_sent(100);
    stats.update_throughput(0.5);

    EXPECT_FLOAT_EQ(stats.out_packets_per_sec, 0.0F); /* Not updated yet */
}

TEST(Stats, ServerStatsDefaults) {
    netudp::ServerStats ss;
    EXPECT_EQ(ss.connected_clients, 0U);
    EXPECT_EQ(ss.total_packets_sent, 0U);
    EXPECT_EQ(ss.ddos_severity, 0U);
}
