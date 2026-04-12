#ifndef NETUDP_RTT_H
#define NETUDP_RTT_H

/**
 * @file rtt.h
 * @brief RTT estimation per RFC 6298 (spec 07 REQ-07.4).
 *
 * Measured from piggybacked ack_delay field — no separate ping/pong.
 * sample_rtt = (now - send_time_of_acked_packet) - ack_delay_us
 */

#include "../core/log.h"
#include "../profiling/profiler.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace netudp {

class RttEstimator {
public:
    static constexpr double MIN_RTO = 0.100;  /* 100ms */
    static constexpr double MAX_RTO = 2.000;  /* 2000ms */
    static constexpr double INITIAL_RTO = 1.0; /* 1000ms */

    /**
     * Process a new RTT sample.
     * @param send_time  When we sent the packet that was acked
     * @param now        Current time
     * @param ack_delay_us  Microseconds the remote held the ack before sending
     */
    void on_sample(double send_time, double now, uint16_t ack_delay_us) {
        NETUDP_ZONE("rtt::on_sample");
        double sample = (now - send_time) - (static_cast<double>(ack_delay_us) / 1000000.0);
        if (sample < 0.0) {
            sample = 0.001; /* Floor at 1ms */
        }

        if (!has_sample_) {
            srtt_ = sample;
            rttvar_ = sample / 2.0;
            has_sample_ = true;
        } else {
            double alpha = 0.125;
            double beta = 0.25;
            rttvar_ = (1.0 - beta) * rttvar_ + beta * std::abs(srtt_ - sample);
            srtt_ = (1.0 - alpha) * srtt_ + alpha * sample;
        }

        rto_ = srtt_ + 4.0 * rttvar_;
        rto_ = std::clamp(rto_, MIN_RTO, MAX_RTO);
    }

    double srtt() const { return srtt_; }
    double rttvar() const { return rttvar_; }
    double rto() const { return rto_; }
    uint32_t ping_ms() const { return static_cast<uint32_t>(srtt_ * 1000.0); }
    bool has_samples() const { return has_sample_; }

    void reset() {
        srtt_ = 0.0;
        rttvar_ = 0.0;
        rto_ = INITIAL_RTO;
        has_sample_ = false;
    }

private:
    double srtt_ = 0.0;
    double rttvar_ = 0.0;
    double rto_ = INITIAL_RTO;
    bool   has_sample_ = false;
};

} // namespace netudp

#endif /* NETUDP_RTT_H */
