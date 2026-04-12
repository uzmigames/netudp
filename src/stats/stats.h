#ifndef NETUDP_STATS_H
#define NETUDP_STATS_H

/**
 * @file stats.h
 * @brief Connection, channel, and server statistics (spec 12).
 *
 * Stats are accumulated during update() and copied on query (O(1)).
 * Throughput uses EMA with 1-second windows.
 */

#include <cstdint>

namespace netudp {

/** Per-connection stats (spec 12 REQ-12.1). */
struct ConnectionStats {
    /* Timing */
    uint32_t ping_ms = 0;
    float    connection_quality_local = 1.0F;
    float    connection_quality_remote = 1.0F;

    /* Throughput (EMA, updated every second) */
    float    out_packets_per_sec = 0.0F;
    float    out_bytes_per_sec = 0.0F;
    float    in_packets_per_sec = 0.0F;
    float    in_bytes_per_sec = 0.0F;

    /* Capacity */
    uint32_t send_rate_bytes_per_sec = 0;
    uint32_t max_send_rate_bytes_per_sec = 0;

    /* Queue depth */
    uint32_t pending_unreliable_bytes = 0;
    uint32_t pending_reliable_bytes = 0;
    uint32_t sent_unacked_reliable_bytes = 0;
    uint64_t queue_time_us = 0;

    /* Reliability */
    uint32_t packets_sent = 0;
    uint32_t packets_received = 0;
    uint32_t packets_lost = 0;
    uint32_t packets_out_of_order = 0;
    uint32_t messages_sent = 0;
    uint32_t messages_received = 0;
    uint32_t messages_dropped = 0;
    uint32_t window_stalls = 0;

    /* Fragments */
    uint32_t fragments_sent = 0;
    uint32_t fragments_received = 0;
    uint32_t fragments_retransmitted = 0;
    uint32_t fragments_timed_out = 0;

    /* Compression */
    float    compression_ratio = 0.0F;
    uint64_t compression_bytes_saved = 0;

    /* Security */
    uint32_t replay_attacks_blocked = 0;
    uint32_t decrypt_failures = 0;

    /* Throughput accumulation (internal, reset every second) */
    uint32_t out_packets_this_sec_ = 0;
    uint32_t out_bytes_this_sec_ = 0;
    uint32_t in_packets_this_sec_ = 0;
    uint32_t in_bytes_this_sec_ = 0;
    double   last_throughput_time_ = 0.0;

    /** Update throughput EMA. Call with current time every update(). */
    void update_throughput(double now) {
        double elapsed = now - last_throughput_time_;
        if (elapsed >= 1.0) {
            float alpha = 0.2F;
            out_packets_per_sec = (1.0F - alpha) * out_packets_per_sec +
                                   alpha * static_cast<float>(out_packets_this_sec_);
            out_bytes_per_sec = (1.0F - alpha) * out_bytes_per_sec +
                                 alpha * static_cast<float>(out_bytes_this_sec_);
            in_packets_per_sec = (1.0F - alpha) * in_packets_per_sec +
                                  alpha * static_cast<float>(in_packets_this_sec_);
            in_bytes_per_sec = (1.0F - alpha) * in_bytes_per_sec +
                                alpha * static_cast<float>(in_bytes_this_sec_);

            out_packets_this_sec_ = 0;
            out_bytes_this_sec_ = 0;
            in_packets_this_sec_ = 0;
            in_bytes_this_sec_ = 0;
            last_throughput_time_ = now;
        }
    }

    void on_packet_sent(int bytes) {
        packets_sent++;
        out_packets_this_sec_++;
        out_bytes_this_sec_ += static_cast<uint32_t>(bytes);
    }

    void on_packet_received(int bytes) {
        packets_received++;
        in_packets_this_sec_++;
        in_bytes_this_sec_ += static_cast<uint32_t>(bytes);
    }
};

/** Per-channel stats (spec 12 REQ-12.2). */
struct ChannelStatsSnapshot {
    uint32_t messages_sent = 0;
    uint32_t messages_received = 0;
    uint32_t messages_dropped = 0;
    uint32_t bytes_sent = 0;
    uint32_t bytes_received = 0;
    uint32_t pending_bytes = 0;
    uint32_t unacked_reliable_bytes = 0;
    uint64_t queue_time_us = 0;
    float    compression_ratio = 0.0F;
};

/** Global server stats (spec 12 REQ-12.3). */
struct ServerStats {
    uint32_t connected_clients = 0;
    uint32_t max_clients = 0;
    uint64_t total_packets_sent = 0;
    uint64_t total_packets_received = 0;
    uint64_t total_bytes_sent = 0;
    uint64_t total_bytes_received = 0;
    float    packets_per_sec_in = 0.0F;
    float    packets_per_sec_out = 0.0F;
    uint8_t  ddos_severity = 0;
    uint32_t ddos_bad_packets_per_sec = 0;
};

} // namespace netudp

#endif /* NETUDP_STATS_H */
