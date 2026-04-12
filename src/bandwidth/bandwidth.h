#ifndef NETUDP_BANDWIDTH_H
#define NETUDP_BANDWIDTH_H

/**
 * @file bandwidth.h
 * @brief Per-connection bandwidth control: token bucket, QueuedBits, AIMD congestion.
 */

#include "../core/log.h"
#include "../profiling/profiler.h"
#include <algorithm>
#include <cstdint>

namespace netudp {

/** Per-connection token bucket rate limiter (spec 10 REQ-10.1). */
struct BandwidthBucket {
    uint32_t rate_bytes_per_sec = 256 * 1024; /* 256 KB/s default */
    uint32_t burst_bytes = 32 * 1024;         /* 32 KB burst */
    double   tokens = 0.0;
    double   last_refill_time = 0.0;

    void init(double time) {
        tokens = static_cast<double>(burst_bytes);
        last_refill_time = time;
    }

    void refill(double now) {
        NETUDP_ZONE("bw::refill");
        double elapsed = now - last_refill_time;
        if (elapsed > 0.0) {
            tokens += static_cast<double>(rate_bytes_per_sec) * elapsed;
            tokens = std::min(tokens, static_cast<double>(burst_bytes));
            last_refill_time = now;
        }
    }

    bool try_consume(int bytes) {
        NETUDP_ZONE("bw::try_consume");
        double needed = static_cast<double>(bytes);
        if (tokens >= needed) {
            tokens -= needed;
            return true;
        }
        return false;
    }
};

/** Per-connection QueuedBits budget (spec 10 REQ-10.2). */
struct QueuedBitsBudget {
    int32_t queued_bits = 0;
    int32_t burst_bits = 32 * 1024 * 8; /* burst_bytes × 8 */

    void refill(double delta_time, uint32_t send_rate_bps) {
        queued_bits -= static_cast<int32_t>(static_cast<double>(send_rate_bps) * 8.0 * delta_time);
        if (queued_bits < -burst_bits) {
            queued_bits = -burst_bits;
        }
    }

    void consume(int packet_bytes) {
        queued_bits += packet_bytes * 8;
    }

    bool can_send() const {
        return queued_bits <= 0;
    }
};

/**
 * AIMD congestion control (spec 10 REQ-10.3).
 * Evaluates loss rate from a sliding window of ack results.
 */
class CongestionControl {
public:
    static constexpr uint32_t MIN_SEND_RATE = 32 * 1024;       /* 32 KB/s */
    static constexpr uint32_t DEFAULT_MAX_RATE = 256 * 1024;    /* 256 KB/s */
    static constexpr float DECREASE_FACTOR = 0.75F;
    static constexpr float INCREASE_FACTOR = 1.10F;
    static constexpr float LOSS_THRESHOLD_HIGH = 0.05F;
    static constexpr float LOSS_THRESHOLD_LOW = 0.01F;
    static constexpr int WINDOW_SIZE = 64;

    void init(uint32_t max_rate = DEFAULT_MAX_RATE) {
        send_rate_ = max_rate;
        max_send_rate_ = max_rate;
        total_packets_ = 0;
        lost_packets_ = 0;
        rtt_samples_ = 0;
    }

    /** Record a packet ack result. */
    void on_packet_acked() {
        total_packets_++;
        if (total_packets_ > WINDOW_SIZE) {
            total_packets_ = WINDOW_SIZE;
            if (lost_packets_ > 0) {
                lost_packets_--;
            }
        }
    }

    void on_packet_lost() {
        total_packets_++;
        lost_packets_++;
        if (total_packets_ > WINDOW_SIZE) {
            total_packets_ = WINDOW_SIZE;
        }
    }

    void on_rtt_sample() {
        rtt_samples_++;
    }

    /** Evaluate and adjust send rate. Call once per RTT interval. */
    void evaluate() {
        NETUDP_ZONE("cong::evaluate");
        if (total_packets_ < 10) {
            return; /* Not enough data */
        }

        float loss_rate = static_cast<float>(lost_packets_) / static_cast<float>(total_packets_);

        if (loss_rate > LOSS_THRESHOLD_HIGH) {
            /* Multiplicative decrease */
            uint32_t old_rate = send_rate_;
            send_rate_ = static_cast<uint32_t>(static_cast<float>(send_rate_) * DECREASE_FACTOR);
            if (send_rate_ < MIN_SEND_RATE) {
                send_rate_ = MIN_SEND_RATE;
            }
            NLOG_DEBUG("[netudp] congestion: rate decreased old=%u new=%u loss=%.1f%%",
                       old_rate, send_rate_, static_cast<double>(loss_rate) * 100.0);
        } else if (loss_rate < LOSS_THRESHOLD_LOW && rtt_samples_ >= 10) {
            /* Additive increase */
            uint32_t old_rate = send_rate_;
            send_rate_ = static_cast<uint32_t>(static_cast<float>(send_rate_) * INCREASE_FACTOR);
            if (send_rate_ > max_send_rate_) {
                send_rate_ = max_send_rate_;
            }
            NLOG_DEBUG("[netudp] congestion: rate increased old=%u new=%u",
                       old_rate, send_rate_);
        }
    }

    uint32_t send_rate() const { return send_rate_; }
    uint32_t max_send_rate() const { return max_send_rate_; }
    float loss_rate() const {
        if (total_packets_ == 0) {
            return 0.0F;
        }
        return static_cast<float>(lost_packets_) / static_cast<float>(total_packets_);
    }

    void reset() {
        send_rate_ = max_send_rate_;
        total_packets_ = 0;
        lost_packets_ = 0;
        rtt_samples_ = 0;
    }

private:
    uint32_t send_rate_ = DEFAULT_MAX_RATE;
    uint32_t max_send_rate_ = DEFAULT_MAX_RATE;
    int total_packets_ = 0;
    int lost_packets_ = 0;
    int rtt_samples_ = 0;
};

} // namespace netudp

#endif /* NETUDP_BANDWIDTH_H */
