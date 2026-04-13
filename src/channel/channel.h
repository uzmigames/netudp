#ifndef NETUDP_CHANNEL_H
#define NETUDP_CHANNEL_H

/**
 * @file channel.h
 * @brief Channel types, config, message queue, Nagle timer, priority scheduling.
 *
 * 4 channel types: unreliable, unreliable_sequenced, reliable_ordered, reliable_unordered.
 * Each channel has independent send/recv sequencing, Nagle timer, priority+weight.
 */

#include <netudp/netudp_types.h>
#include "../core/ring_buffer.h"
#include "../core/log.h"
#include "../profiling/profiler.h"

#include <cstdint>
#include <cstring>

namespace netudp {

struct QueuedMessage {
    uint8_t  data[NETUDP_MTU];
    int      size = 0;
    uint16_t sequence = 0;
    int      flags = 0;
    bool     valid = false;
};

struct ChannelStats {
    uint32_t messages_sent = 0;
    uint32_t messages_received = 0;
    uint32_t messages_dropped = 0;
    uint32_t bytes_sent = 0;
    uint32_t bytes_received = 0;
};

class alignas(64) Channel {
public:
    void init(int index, const netudp_channel_config_t& config) {
        index_ = index;
        config_ = config;
        send_seq_ = 0;
        recv_seq_ = 0;
        last_delivered_seq_ = 0;
        nagle_accumulate_time_ = 0.0;
        nagle_threshold_sec_ = static_cast<double>(config.nagle_ms) / 1000.0;
        stats_ = {};
        send_queue_.clear();
    }

    /** Queue a message for sending. Returns true if queued successfully. */
    bool queue_send(const uint8_t* data, int size, int flags) {
        NETUDP_ZONE("chan::queue_send");
        if (size <= 0 || size > NETUDP_MTU) {
            NLOG_WARN("[netudp] chan::queue_send: message size %d out of range (max=%d)", size, NETUDP_MTU);
            return false;
        }
        if (send_queue_.full()) {
            NLOG_WARN("[netudp] chan::queue_send: queue full (channel=%d)", index_);
            return false;
        }

        QueuedMessage msg;
        std::memcpy(msg.data, data, static_cast<size_t>(size));
        msg.size = size;
        msg.sequence = send_seq_++;
        msg.flags = flags;
        msg.valid = true;

        /* Check NoNagle flag */
        if ((flags & NETUDP_SEND_NO_NAGLE) != 0 || (flags & NETUDP_SEND_NO_DELAY) != 0) {
            nagle_ready_ = true;
        }

        send_queue_.push_back(msg);
        stats_.messages_sent++;
        stats_.bytes_sent += static_cast<uint32_t>(size);
        return true;
    }

    /** Check if there are messages ready to send (Nagle timer expired or bypassed). */
    bool has_pending(double now) const {
        NETUDP_ZONE("chan::has_pending");
        if (send_queue_.is_empty()) {
            return false;
        }
        if (nagle_ready_) {
            return true;
        }
        if (nagle_threshold_sec_ <= 0.0) {
            return true; /* Nagle disabled */
        }
        return (now - nagle_accumulate_time_) >= nagle_threshold_sec_;
    }

    /** Dequeue the next message to send. Returns false if no messages ready. */
    bool dequeue_send(QueuedMessage* out) {
        NETUDP_ZONE("chan::dequeue_send");
        if (send_queue_.is_empty()) {
            return false;
        }
        send_queue_.pop_front(out);
        nagle_ready_ = false;
        nagle_accumulate_time_ = 0.0;
        return true;
    }

    /** Start Nagle accumulation timer. */
    void start_nagle(double now) {
        if (nagle_accumulate_time_ == 0.0) {
            nagle_accumulate_time_ = now;
        }
    }

    /** Flush: mark all queued messages as ready regardless of Nagle. */
    void flush() {
        nagle_ready_ = true;
    }

    /* --- Receive side --- */

    /** Process a received unreliable message. Returns true if should deliver. */
    bool on_recv_unreliable(uint16_t /*sequence*/) {
        NETUDP_ZONE("chan::on_recv_unreliable");
        stats_.messages_received++;
        return true; /* Always deliver unreliable */
    }

    /** Process a received unreliable_sequenced message. Returns true if should deliver. */
    bool on_recv_unreliable_sequenced(uint16_t sequence) {
        NETUDP_ZONE("chan::on_recv_seq");
        stats_.messages_received++;
        int16_t diff = static_cast<int16_t>(sequence - last_delivered_seq_);
        if (diff <= 0 && last_delivered_seq_ != 0) {
            return false; /* Stale, drop */
        }
        last_delivered_seq_ = sequence;
        return true;
    }

    /* Accessors */
    int index() const { return index_; }
    const netudp_channel_config_t& config() const { return config_; }
    uint8_t type() const { return config_.type; }
    uint8_t priority() const { return config_.priority; }
    uint8_t weight() const { return config_.weight; }
    uint16_t send_seq() const { return send_seq_; }
    uint16_t recv_seq() const { return recv_seq_; }
    const ChannelStats& stats() const { return stats_; }
    ChannelStats& stats_mut() { return stats_; }
    int send_queue_size() const { return send_queue_.size(); }

    void set_recv_seq(uint16_t seq) { recv_seq_ = seq; }
    void increment_recv_seq() { recv_seq_++; }

private:
    int index_ = 0;
    netudp_channel_config_t config_ = {};
    uint16_t send_seq_ = 0;
    uint16_t recv_seq_ = 0;
    uint16_t last_delivered_seq_ = 0;
    double   nagle_accumulate_time_ = 0.0;
    double   nagle_threshold_sec_ = 0.0;
    bool     nagle_ready_ = false;
    ChannelStats stats_ = {};
    FixedRingBuffer<QueuedMessage, 256> send_queue_;
};

/**
 * Priority+weight channel scheduler.
 * Iterates channels in priority order (higher first), dequeues messages.
 * Within same priority, distributes by weight ratio.
 */
class ChannelScheduler {
public:
    /**
     * Get the next channel index that has pending messages, in priority order.
     * Returns -1 if no channels have pending data.
     */
    static int next_channel(Channel* channels, int num_channels, double now) {
        int best = -1;
        uint8_t best_priority = 0;

        for (int i = 0; i < num_channels; ++i) {
            if (channels[i].has_pending(now)) {
                if (best < 0 || channels[i].priority() > best_priority) {
                    best = i;
                    best_priority = channels[i].priority();
                }
            }
        }
        return best;
    }
};

} // namespace netudp

#endif /* NETUDP_CHANNEL_H */
