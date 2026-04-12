#ifndef NETUDP_FRAGMENT_H
#define NETUDP_FRAGMENT_H

/**
 * @file fragment.h
 * @brief Fragmentation and reassembly for messages > MTU (spec 08).
 *
 * Splitting: messages larger than max_fragment_payload are split into
 * up to 255 fragments. Each fragment has a 4-byte header.
 *
 * Reassembly: per-connection pre-allocated slots track incoming fragments
 * via bitmask. Only missing fragments are retransmitted.
 */

#include <netudp/netudp_config.h>
#include "../core/log.h"
#include "../profiling/profiler.h"
#include <cstdint>
#include <cstring>
#include <new>

namespace netudp {

static constexpr int MAX_FRAGMENT_COUNT = 255;
static constexpr int DEFAULT_MAX_MESSAGE_SIZE = 64 * 1024;
static constexpr int ABSOLUTE_MAX_MESSAGE_SIZE = 288 * 1024;
static constexpr int FRAGMENT_HEADER_SIZE = 4;
static constexpr int MAX_CONCURRENT_REASSEMBLIES = 16;
static constexpr double FRAGMENT_TIMEOUT_SEC = 5.0;

struct FragmentHeader {
    uint16_t message_id;
    uint8_t  fragment_index;
    uint8_t  fragment_count;
};

/** Write fragment header to buffer (4 bytes). */
inline int write_fragment_header(const FragmentHeader& hdr, uint8_t* buf) {
    std::memcpy(buf, &hdr.message_id, 2);
    buf[2] = hdr.fragment_index;
    buf[3] = hdr.fragment_count;
    return FRAGMENT_HEADER_SIZE;
}

/** Read fragment header from buffer. */
inline FragmentHeader read_fragment_header(const uint8_t* buf) {
    FragmentHeader hdr = {};
    std::memcpy(&hdr.message_id, buf, 2);
    hdr.fragment_index = buf[2];
    hdr.fragment_count = buf[3];
    return hdr;
}

/**
 * Calculate number of fragments needed for a message.
 * Returns fragment count (1-255), or -1 if message too large.
 */
inline int calc_fragment_count(int message_size, int max_fragment_payload) {
    if (message_size <= 0 || max_fragment_payload <= 0) {
        return -1;
    }
    int count = (message_size + max_fragment_payload - 1) / max_fragment_payload;
    if (count > MAX_FRAGMENT_COUNT) {
        return -1;
    }
    return count;
}

/** Per-message reassembly tracker. */
struct FragmentTracker {
    uint16_t message_id = 0;
    uint8_t  total_fragments = 0;
    uint8_t  received_count = 0;
    uint8_t  received_mask[32] = {}; /* Bitmask: up to 256 fragments */
    double   first_recv_time = 0.0;
    double   last_recv_time = 0.0;
    bool     active = false;

    /* Reassembly buffer — pre-allocated per slot */
    uint8_t* buffer = nullptr;
    int      buffer_capacity = 0;
    int      fragment_sizes[MAX_FRAGMENT_COUNT] = {}; /* Size of each received fragment */

    bool is_complete() const {
        return received_count == total_fragments;
    }

    bool has_fragment(int index) const {
        return (received_mask[index / 8] & (1U << (index % 8))) != 0;
    }

    void mark_fragment(int index) {
        if (!has_fragment(index)) {
            received_mask[index / 8] |= static_cast<uint8_t>(1U << (index % 8));
            received_count++;
        }
    }

    int next_missing() const {
        for (int i = 0; i < total_fragments; ++i) {
            if (!has_fragment(i)) {
                return i;
            }
        }
        return -1;
    }

    /** Get the total reassembled message size. */
    int total_message_size() const {
        int total = 0;
        for (int i = 0; i < total_fragments; ++i) {
            total += fragment_sizes[i];
        }
        return total;
    }

    void reset() {
        message_id = 0;
        total_fragments = 0;
        received_count = 0;
        std::memset(received_mask, 0, sizeof(received_mask));
        first_recv_time = 0.0;
        last_recv_time = 0.0;
        active = false;
        std::memset(fragment_sizes, 0, sizeof(fragment_sizes));
        if (buffer != nullptr) {
            std::memset(buffer, 0, static_cast<size_t>(buffer_capacity));
        }
    }
};

/**
 * Per-connection fragment reassembly manager.
 * Pre-allocates MAX_CONCURRENT_REASSEMBLIES slots.
 */
class FragmentReassembler {
public:
    bool init(int max_message_size) {
        max_message_size_ = max_message_size;
        for (int i = 0; i < MAX_CONCURRENT_REASSEMBLIES; ++i) {
            trackers_[i].reset();
            trackers_[i].buffer = new (std::nothrow) uint8_t[static_cast<size_t>(max_message_size)];
            trackers_[i].buffer_capacity = max_message_size;
            if (trackers_[i].buffer == nullptr) {
                destroy();
                return false;
            }
        }
        return true;
    }

    void destroy() {
        for (int i = 0; i < MAX_CONCURRENT_REASSEMBLIES; ++i) {
            delete[] trackers_[i].buffer;
            trackers_[i].buffer = nullptr;
            trackers_[i].reset();
        }
    }

    ~FragmentReassembler() { destroy(); }

    /**
     * Process a received fragment. Returns pointer to completed message buffer
     * and sets out_size, or nullptr if not yet complete.
     */
    const uint8_t* on_fragment_received(
        uint16_t message_id, uint8_t fragment_index, uint8_t fragment_count,
        const uint8_t* fragment_data, int fragment_len,
        int max_fragment_payload, double time, int* out_size
    ) {
        NETUDP_ZONE("frag::reassemble");
        if (fragment_count == 0 || fragment_index >= fragment_count) {
            return nullptr;
        }

        /* Find or create tracker for this message_id */
        FragmentTracker* tracker = find_tracker(message_id);
        if (tracker == nullptr) {
            tracker = allocate_tracker(message_id, fragment_count, time);
            if (tracker == nullptr) {
                NLOG_WARN("[netudp] frag: all %d reassembly slots in use — dropping fragment (msg_id=%u)",
                          MAX_CONCURRENT_REASSEMBLIES, (unsigned)message_id);
                return nullptr; /* No free slots */
            }
        }

        if (tracker->total_fragments != fragment_count) {
            NLOG_DEBUG("[netudp] frag: fragment count mismatch (msg_id=%u, expected=%u, got=%u)",
                       (unsigned)message_id, (unsigned)tracker->total_fragments, (unsigned)fragment_count);
            return nullptr; /* Mismatch */
        }

        /* Already received this fragment? */
        if (tracker->has_fragment(fragment_index)) {
            return nullptr;
        }

        /* Copy fragment data into reassembly buffer */
        int offset = fragment_index * max_fragment_payload;
        if (offset + fragment_len > tracker->buffer_capacity) {
            NLOG_WARN("[netudp] frag: buffer overflow (msg_id=%u, offset=%d, len=%d, cap=%d)",
                      (unsigned)message_id, offset, fragment_len, tracker->buffer_capacity);
            return nullptr; /* Would overflow */
        }

        std::memcpy(tracker->buffer + offset, fragment_data, static_cast<size_t>(fragment_len));
        tracker->fragment_sizes[fragment_index] = fragment_len;
        tracker->mark_fragment(fragment_index);
        tracker->last_recv_time = time;

        NLOG_TRACE("[netudp] frag: received %u/%u (msg_id=%u)",
                   (unsigned)tracker->received_count, (unsigned)tracker->total_fragments, (unsigned)message_id);

        if (tracker->is_complete()) {
            *out_size = tracker->total_message_size();
            const uint8_t* result = tracker->buffer;
            NLOG_DEBUG("[netudp] frag: reassembly complete (msg_id=%u, size=%d)", (unsigned)message_id, *out_size);
            /* Reset tracker after delivering (caller must copy data before next call) */
            tracker->active = false;
            return result;
        }

        return nullptr;
    }

    /** Cleanup timed-out reassemblies. Returns number cleaned. */
    int cleanup_timeout(double now) {
        NETUDP_ZONE("frag::cleanup");
        int cleaned = 0;
        for (int i = 0; i < MAX_CONCURRENT_REASSEMBLIES; ++i) {
            if (trackers_[i].active && (now - trackers_[i].first_recv_time) > FRAGMENT_TIMEOUT_SEC) {
                NLOG_DEBUG("[netudp] frag: timeout cleanup (msg_id=%u, age=%.1fs)",
                           (unsigned)trackers_[i].message_id,
                           now - trackers_[i].first_recv_time);
                trackers_[i].reset();
                trackers_[i].active = false;
                cleaned++;
            }
        }
        return cleaned;
    }

private:
    FragmentTracker* find_tracker(uint16_t message_id) {
        NETUDP_ZONE("frag::find_tracker");
        for (int i = 0; i < MAX_CONCURRENT_REASSEMBLIES; ++i) {
            if (trackers_[i].active && trackers_[i].message_id == message_id) {
                return &trackers_[i];
            }
        }
        return nullptr;
    }

    FragmentTracker* allocate_tracker(uint16_t message_id, uint8_t fragment_count, double time) {
        NETUDP_ZONE("frag::alloc_tracker");
        for (int i = 0; i < MAX_CONCURRENT_REASSEMBLIES; ++i) {
            if (!trackers_[i].active) {
                trackers_[i].reset();
                trackers_[i].message_id = message_id;
                trackers_[i].total_fragments = fragment_count;
                trackers_[i].first_recv_time = time;
                trackers_[i].active = true;
                return &trackers_[i];
            }
        }
        return nullptr;
    }

    FragmentTracker trackers_[MAX_CONCURRENT_REASSEMBLIES] = {};
    int max_message_size_ = DEFAULT_MAX_MESSAGE_SIZE;
};

} // namespace netudp

#endif /* NETUDP_FRAGMENT_H */
