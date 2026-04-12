#ifndef NETUDP_PACKET_TRACKER_H
#define NETUDP_PACKET_TRACKER_H

/**
 * @file packet_tracker.h
 * @brief Packet-level sequence + ack bitmask tracking (spec 07 REQ-07.1/07.2).
 *
 * Every outgoing data packet gets a monotonic 16-bit sequence number.
 * Every packet carries AckFields (8 bytes): ack + ack_bits + ack_delay_us.
 * The sender uses ack_bits to determine which packets were received.
 */

#include <cstdint>
#include <cstring>

namespace netudp {

/** 8 bytes, first bytes of encrypted payload (spec 07/09). */
struct AckFields {
    uint16_t ack;            /* Highest received sequence from remote */
    uint32_t ack_bits;       /* Bitmask: bit N = received (ack - N - 1) */
    uint16_t ack_delay_us;   /* Microseconds since receiving 'ack' */
};

struct SentPacketRecord {
    double   send_time;
    uint16_t sequence;
    bool     acked;
};

static constexpr int PACKET_WINDOW_SIZE = 33; /* Matches ack_bits (32 bits) + ack itself */

class PacketTracker {
public:
    /* --- Sending --- */

    /** Get next send sequence and record the packet. */
    uint16_t send_packet(double time) {
        uint16_t seq = send_sequence_++;
        int idx = seq % PACKET_WINDOW_SIZE;
        sent_[idx].sequence = seq;
        sent_[idx].send_time = time;
        sent_[idx].acked = false;
        return seq;
    }

    /** Check if window is full (33 unacked packets). */
    bool is_window_full() const {
        uint16_t dist = send_sequence_ - oldest_unacked_;
        return dist >= PACKET_WINDOW_SIZE;
    }

    uint16_t send_sequence() const { return send_sequence_; }
    uint16_t oldest_unacked() const { return oldest_unacked_; }

    /* --- Receiving --- */

    /** Called when we receive a packet with this sequence number. */
    void on_packet_received(uint16_t sequence, double time) {
        /* Track for ack generation */
        if (!received_any_) {
            recv_ack_ = sequence;
            recv_time_ = time;
            received_any_ = true;
        } else {
            /* Check if this is newer (handling 16-bit wrap) */
            int16_t diff = static_cast<int16_t>(sequence - recv_ack_);
            if (diff > 0) {
                /* Shift ack_bits: new packets between old ack and new ack */
                uint16_t shift = static_cast<uint16_t>(diff);
                if (shift < 32) {
                    recv_ack_bits_ = (recv_ack_bits_ << shift) | (1U << (shift - 1));
                } else {
                    recv_ack_bits_ = 0;
                }
                recv_ack_ = sequence;
                recv_time_ = time;
            } else if (diff < 0 && diff > -32) {
                /* Older packet within ack_bits window */
                int bit = static_cast<int>(-diff) - 1;
                recv_ack_bits_ |= (1U << bit);
            }
        }
    }

    /** Build AckFields to include in outgoing packet. */
    AckFields build_ack_fields(double now) const {
        AckFields fields = {};
        fields.ack = recv_ack_;
        fields.ack_bits = recv_ack_bits_;
        if (received_any_) {
            double delay = now - recv_time_;
            if (delay < 0.0) {
                delay = 0.0;
            }
            uint32_t delay_us = static_cast<uint32_t>(delay * 1000000.0);
            fields.ack_delay_us = delay_us > 65535 ? 65535 : static_cast<uint16_t>(delay_us);
        }
        return fields;
    }

    /* --- Processing received acks --- */

    /** Process ack fields from a received packet. Returns number of newly acked packets. */
    int process_acks(const AckFields& fields) {
        int newly_acked = 0;

        /* The ack field itself */
        if (mark_acked(fields.ack)) {
            ++newly_acked;
        }

        /* ack_bits: bit N means packet (ack - N - 1) was received */
        for (int i = 0; i < 32; ++i) {
            if ((fields.ack_bits & (1U << i)) != 0) {
                uint16_t seq = fields.ack - static_cast<uint16_t>(i) - 1;
                if (mark_acked(seq)) {
                    ++newly_acked;
                }
            }
        }

        /* Advance oldest_unacked */
        while (oldest_unacked_ != send_sequence_) {
            int idx = oldest_unacked_ % PACKET_WINDOW_SIZE;
            if (!sent_[idx].acked || sent_[idx].sequence != oldest_unacked_) {
                break;
            }
            oldest_unacked_++;
        }

        return newly_acked;
    }

    /** Check if a specific sent packet was acked. */
    bool is_acked(uint16_t seq) const {
        int idx = seq % PACKET_WINDOW_SIZE;
        return sent_[idx].acked && sent_[idx].sequence == seq;
    }

    /** Get send time of a packet (for RTT calculation). */
    double get_send_time(uint16_t seq) const {
        int idx = seq % PACKET_WINDOW_SIZE;
        if (sent_[idx].sequence == seq) {
            return sent_[idx].send_time;
        }
        return -1.0;
    }

    void reset() {
        send_sequence_ = 0;
        oldest_unacked_ = 0;
        recv_ack_ = 0;
        recv_ack_bits_ = 0;
        recv_time_ = 0.0;
        received_any_ = false;
        std::memset(sent_, 0, sizeof(sent_));
    }

private:
    bool mark_acked(uint16_t seq) {
        int idx = seq % PACKET_WINDOW_SIZE;
        if (sent_[idx].sequence == seq && !sent_[idx].acked) {
            sent_[idx].acked = true;
            return true;
        }
        return false;
    }

    uint16_t send_sequence_ = 0;
    uint16_t oldest_unacked_ = 0;

    /* Receive-side ack state */
    uint16_t recv_ack_ = 0;
    uint32_t recv_ack_bits_ = 0;
    double   recv_time_ = 0.0;
    bool     received_any_ = false;

    /* Sent packet records */
    SentPacketRecord sent_[PACKET_WINDOW_SIZE] = {};
};

/** Serialize AckFields to buffer (8 bytes). Returns bytes written. */
inline int write_ack_fields(const AckFields& fields, uint8_t* buf) {
    std::memcpy(buf, &fields.ack, 2);
    std::memcpy(buf + 2, &fields.ack_bits, 4);
    std::memcpy(buf + 6, &fields.ack_delay_us, 2);
    return 8;
}

/** Deserialize AckFields from buffer (8 bytes). */
inline AckFields read_ack_fields(const uint8_t* buf) {
    AckFields fields = {};
    std::memcpy(&fields.ack, buf, 2);
    std::memcpy(&fields.ack_bits, buf + 2, 4);
    std::memcpy(&fields.ack_delay_us, buf + 6, 2);
    return fields;
}

} // namespace netudp

#endif /* NETUDP_PACKET_TRACKER_H */
