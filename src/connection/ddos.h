#ifndef NETUDP_DDOS_H
#define NETUDP_DDOS_H

/**
 * @file ddos.h
 * @brief DDoS severity escalation with auto-cooloff (spec 05 REQ-05.7).
 *
 * 5 levels: None → Low → Medium → High → Critical.
 * Escalation based on bad_packets_per_sec. Auto-cooloff after 30s (60s Critical).
 */

#include <cstdint>

namespace netudp {

enum class DDoSSeverity : uint8_t {
    None     = 0,
    Low      = 1,
    Medium   = 2,
    High     = 3,
    Critical = 4
};

class DDoSMonitor {
public:
    void on_bad_packet() {
        bad_packets_this_window_++;
    }

    /** Call every second to evaluate thresholds and manage cooloff. */
    void update(double dt) {
        time_since_last_eval_ += dt;

        if (time_since_last_eval_ >= 1.0) {
            /* Evaluate escalation */
            int bps = bad_packets_this_window_;
            bad_packets_per_sec_ = bps;
            bad_packets_this_window_ = 0;
            time_since_last_eval_ = 0.0;

            DDoSSeverity new_severity = severity_;

            if (bps > 10000) {
                new_severity = DDoSSeverity::Critical;
            } else if (bps > 2000) {
                new_severity = DDoSSeverity::High;
            } else if (bps > 500) {
                new_severity = DDoSSeverity::Medium;
            } else if (bps > 100) {
                new_severity = DDoSSeverity::Low;
            }

            if (new_severity > severity_) {
                severity_ = new_severity;
                cooloff_timer_ = 0.0;
            } else if (bps <= 100) {
                /* Cooloff */
                double cooloff_threshold = (severity_ == DDoSSeverity::Critical) ? 60.0 : 30.0;
                cooloff_timer_ += 1.0;
                if (cooloff_timer_ >= cooloff_threshold) {
                    if (severity_ != DDoSSeverity::None) {
                        severity_ = static_cast<DDoSSeverity>(static_cast<uint8_t>(severity_) - 1);
                        cooloff_timer_ = 0.0;
                    }
                }
            } else {
                cooloff_timer_ = 0.0;
            }
        }
    }

    bool should_process_new_connection() const {
        return severity_ < DDoSSeverity::Critical;
    }

    bool should_process_packet(bool is_established) const {
        if (severity_ >= DDoSSeverity::High && !is_established) {
            return false;
        }
        return true;
    }

    DDoSSeverity severity() const { return severity_; }
    int bad_packets_per_sec() const { return bad_packets_per_sec_; }

    void reset() {
        severity_ = DDoSSeverity::None;
        bad_packets_this_window_ = 0;
        bad_packets_per_sec_ = 0;
        cooloff_timer_ = 0.0;
        time_since_last_eval_ = 0.0;
    }

private:
    DDoSSeverity severity_ = DDoSSeverity::None;
    int bad_packets_this_window_ = 0;
    int bad_packets_per_sec_ = 0;
    double cooloff_timer_ = 0.0;
    double time_since_last_eval_ = 0.0;
};

} // namespace netudp

#endif /* NETUDP_DDOS_H */
