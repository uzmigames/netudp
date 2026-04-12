#ifndef NETUDP_REPLAY_H
#define NETUDP_REPLAY_H

/**
 * @file replay.h
 * @brief Replay protection using 64-bit nonce counter and 256-entry window.
 *
 * Operates on the 64-bit nonce counter (from KeyEpoch), not the 16-bit
 * wire sequence. This avoids wraparound issues. See spec 04 REQ-04.9.
 */

#include <cstdint>
#include <cstring>

namespace netudp {
namespace crypto {

struct ReplayProtection {
    static constexpr int WINDOW_SIZE = 256;

    static constexpr uint64_t EMPTY_SLOT = UINT64_MAX;

    uint64_t most_recent = 0;
    uint64_t received[WINDOW_SIZE];

    ReplayProtection() { reset(); }

    /**
     * Check if a nonce has already been received or is too old.
     * @return true if packet should be REJECTED (duplicate or too old)
     */
    bool is_duplicate(uint64_t nonce) const {
        /* Too old: more than WINDOW_SIZE behind most_recent */
        if (most_recent >= WINDOW_SIZE && nonce + WINDOW_SIZE <= most_recent) {
            return true;
        }

        /* Check if already received (EMPTY_SLOT means slot is unused) */
        uint64_t slot_val = received[nonce % WINDOW_SIZE];
        return slot_val != EMPTY_SLOT && slot_val == nonce;
    }

    /**
     * Mark a nonce as received and update most_recent.
     * Call AFTER successful decryption (not before).
     */
    void advance(uint64_t nonce) {
        received[nonce % WINDOW_SIZE] = nonce;
        if (nonce > most_recent) {
            most_recent = nonce;
        }
    }

    /** Reset the window (e.g., after rekeying). */
    void reset() {
        most_recent = 0;
        std::memset(received, 0xFF, sizeof(received)); /* Fill with EMPTY_SLOT (UINT64_MAX) */
    }
};

} // namespace crypto
} // namespace netudp

#endif /* NETUDP_REPLAY_H */
