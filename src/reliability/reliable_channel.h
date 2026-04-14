#ifndef NETUDP_RELIABLE_CHANNEL_H
#define NETUDP_RELIABLE_CHANNEL_H

/**
 * @file reliable_channel.h
 * @brief Per-channel reliable message sequencing, retransmission, ordered delivery.
 *
 * Layer 2 reliability: independent from packet-level acks (Layer 1).
 * Each reliable channel tracks its own send_seq/recv_seq and retransmit state.
 */

#include "../core/ring_buffer.h"
#include "../core/log.h"
#include "../profiling/profiler.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace netudp {

struct SentMessage {
    uint16_t sequence = 0;
    uint16_t packet_sequence = 0;  /* Which packet carried this message */
    double   send_time = 0.0;
    int      retry_count = 0;
    uint8_t  data[1200] = {};
    int      data_len = 0;
    bool     acked = false;
    bool     valid = false;
};

struct ReceivedMessage {
    uint16_t sequence = 0;
    uint8_t  data[1200] = {};
    int      data_len = 0;
    bool     valid = false;
};

static constexpr int RELIABLE_BUFFER_SIZE = 64;
static constexpr int MAX_RETRIES = 10;

class ReliableChannelState {
public:
    uint16_t send_seq = 0;
    uint16_t recv_seq = 0;       /* Next expected from remote */
    uint16_t oldest_unacked = 0;

    /** Record a sent reliable message. */
    bool record_send(const uint8_t* data, int len, uint16_t packet_seq, double time) {
        NETUDP_ZONE("rel::record_send");
        if (len <= 0 || len > 1200) {
            return false;
        }

        int idx = send_seq % RELIABLE_BUFFER_SIZE;
        SentMessage& msg = sent_buffer_[idx];
        msg.sequence = send_seq;
        msg.packet_sequence = packet_seq;
        msg.send_time = time;
        msg.retry_count = 0;
        std::memcpy(msg.data, data, static_cast<size_t>(len));
        msg.data_len = len;
        msg.acked = false;
        msg.valid = true;

        send_seq++;
        return true;
    }

    /** Mark a message as acked. */
    void mark_acked(uint16_t msg_seq) {
        NETUDP_ZONE("rel::mark_acked");
        int idx = msg_seq % RELIABLE_BUFFER_SIZE;
        if (sent_buffer_[idx].valid && sent_buffer_[idx].sequence == msg_seq) {
            sent_buffer_[idx].acked = true;
        }

        /* Advance oldest_unacked */
        while (oldest_unacked != send_seq) {
            int oidx = oldest_unacked % RELIABLE_BUFFER_SIZE;
            if (!sent_buffer_[oidx].acked || sent_buffer_[oidx].sequence != oldest_unacked) {
                break;
            }
            sent_buffer_[oidx].valid = false;
            oldest_unacked++;
        }
    }

    /** Get a sent message for retransmission. Returns nullptr if acked or invalid. */
    SentMessage* get_for_retransmit(uint16_t msg_seq) {
        int idx = msg_seq % RELIABLE_BUFFER_SIZE;
        SentMessage& msg = sent_buffer_[idx];
        if (msg.valid && msg.sequence == msg_seq && !msg.acked) {
            return &msg;
        }
        return nullptr;
    }

    /** Find messages that need retransmission (timed out based on RTO). */
    int find_retransmits(double now, double rto, uint16_t* out_seqs, int max_count) {
        NETUDP_ZONE("rel::find_retransmits");
        int count = 0;
        for (uint16_t seq = oldest_unacked; seq != send_seq && count < max_count; seq++) {
            int idx = seq % RELIABLE_BUFFER_SIZE;
            SentMessage& msg = sent_buffer_[idx];
            if (!msg.valid || msg.sequence != seq || msg.acked) {
                continue;
            }

            double effective_rto = rto * (1 << (std::min)(msg.retry_count, 5));
            if (now - msg.send_time >= effective_rto) {
                if (msg.retry_count >= MAX_RETRIES) {
                    msg.valid = false; /* Drop after max retries */
                    messages_dropped_++;
                    NLOG_WARN("[netudp] rel: message dropped after %d retries (seq=%u)", MAX_RETRIES, (unsigned)seq);
                    continue;
                }
                out_seqs[count++] = seq;
            }
        }
        return count;
    }

    /* --- Receive: Reliable Ordered --- */

    /** Buffer a received reliable ordered message. Returns true if new (not duplicate). */
    bool buffer_received_ordered(uint16_t msg_seq, const uint8_t* data, int len) {
        NETUDP_ZONE("rel::buffer_recv");
        int16_t diff = static_cast<int16_t>(msg_seq - recv_seq);
        if (diff < 0) {
            return false; /* Already delivered */
        }
        if (diff >= RELIABLE_BUFFER_SIZE) {
            return false; /* Too far ahead */
        }

        int idx = msg_seq % RELIABLE_BUFFER_SIZE;
        if (recv_buffer_[idx].valid && recv_buffer_[idx].sequence == msg_seq) {
            return false; /* Duplicate */
        }

        ReceivedMessage& rmsg = recv_buffer_[idx];
        rmsg.sequence = msg_seq;
        std::memcpy(rmsg.data, data, static_cast<size_t>((std::min)(len, 1200)));
        rmsg.data_len = (std::min)(len, 1200);
        rmsg.valid = true;

        return true;
    }

    /**
     * Deliver contiguous ordered messages starting from recv_seq.
     * Calls callback for each delivered message. Returns count delivered.
     */
    template <typename Fn>
    int deliver_ordered(Fn callback) {
        NETUDP_ZONE("rel::deliver_ordered");
        int delivered = 0;
        while (true) {
            int idx = recv_seq % RELIABLE_BUFFER_SIZE;
            if (!recv_buffer_[idx].valid || recv_buffer_[idx].sequence != recv_seq) {
                break;
            }
            callback(recv_buffer_[idx].data, recv_buffer_[idx].data_len, recv_seq);
            recv_buffer_[idx].valid = false;
            recv_seq++;
            delivered++;
        }
        return delivered;
    }

    /* --- Receive: Reliable Unordered --- */

    /** Check if a sequence was already received (for unordered duplicate detection). */
    bool is_received_unordered(uint16_t msg_seq) const {
        /* Use the recv_buffer as a sparse tracker */
        int idx = msg_seq % RELIABLE_BUFFER_SIZE;
        return recv_buffer_[idx].valid && recv_buffer_[idx].sequence == msg_seq;
    }

    /** Mark a sequence as received for unordered duplicate detection. */
    void mark_received_unordered(uint16_t msg_seq) {
        int idx = msg_seq % RELIABLE_BUFFER_SIZE;
        recv_buffer_[idx].sequence = msg_seq;
        recv_buffer_[idx].valid = true;
    }

    uint32_t messages_dropped() const { return messages_dropped_; }

    void reset() {
        send_seq = 0;
        recv_seq = 0;
        oldest_unacked = 0;
        messages_dropped_ = 0;
        for (auto& s : sent_buffer_) { s = SentMessage{}; }
        for (auto& r : recv_buffer_) { r = ReceivedMessage{}; }
    }

private:
    SentMessage sent_buffer_[RELIABLE_BUFFER_SIZE] = {};
    ReceivedMessage recv_buffer_[RELIABLE_BUFFER_SIZE] = {};
    uint32_t messages_dropped_ = 0;
};

} // namespace netudp

#endif /* NETUDP_RELIABLE_CHANNEL_H */
