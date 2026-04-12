#ifndef NETUDP_RATE_LIMITER_H
#define NETUDP_RATE_LIMITER_H

/**
 * @file rate_limiter.h
 * @brief Per-IP token bucket rate limiter (spec 05 REQ-05.6).
 *
 * Applied BEFORE any token processing. If bucket empty, packet is
 * silently dropped (no response — prevents amplification).
 */

#include "../core/hash_map.h"
#include "../core/address.h"
#include <netudp/netudp_types.h>

#include <cstdint>
#include <cstring>

namespace netudp {

struct RateBucket {
    double   tokens;
    double   last_refill_time;
    double   last_activity_time;
};

class RateLimiter {
public:
    static constexpr int    RATE       = 60;   /* packets/sec */
    static constexpr int    BURST      = 10;
    static constexpr double EXPIRY_SEC = 30.0; /* Evict after 30s inactivity */

    /**
     * Check if a packet from this address should be allowed.
     * Returns true if allowed, false if rate-limited (drop silently).
     */
    bool allow(const netudp_address_t* addr, double now) {
        AddressKey key = make_key(addr);
        RateBucket* bucket = buckets_.find(key);

        if (bucket == nullptr) {
            /* New IP — insert with full burst */
            RateBucket new_bucket;
            new_bucket.tokens = static_cast<double>(BURST - 1); /* Consume one for this packet */
            new_bucket.last_refill_time = now;
            new_bucket.last_activity_time = now;
            buckets_.insert(key, new_bucket);
            return true;
        }

        /* Refill tokens */
        double elapsed = now - bucket->last_refill_time;
        if (elapsed > 0.0) {
            bucket->tokens += RATE * elapsed;
            if (bucket->tokens > static_cast<double>(BURST)) {
                bucket->tokens = static_cast<double>(BURST);
            }
            bucket->last_refill_time = now;
        }

        bucket->last_activity_time = now;

        /* Try consume */
        if (bucket->tokens >= 1.0) {
            bucket->tokens -= 1.0;
            return true;
        }

        return false; /* Rate limited */
    }

    /** Remove expired entries (call periodically, e.g., every second). */
    void cleanup(double now) {
        /* Iterate and mark expired — can't remove during for_each, so collect keys */
        AddressKey expired_keys[64];
        int expired_count = 0;

        buckets_.for_each([&](const AddressKey& key, RateBucket& bucket) -> bool {
            if (now - bucket.last_activity_time > EXPIRY_SEC && expired_count < 64) {
                expired_keys[expired_count++] = key;
            }
            return true;
        });

        for (int i = 0; i < expired_count; ++i) {
            buckets_.remove(expired_keys[i]);
        }
    }

    int size() const { return buckets_.size(); }

private:
    /* Compact address key: type(1) + data(16) + port(2) = 19 bytes, padded to 20 */
    struct AddressKey {
        uint8_t bytes[20] = {};
    };

    static AddressKey make_key(const netudp_address_t* addr) {
        AddressKey key;
        std::memset(&key, 0, sizeof(key));
        key.bytes[0] = addr->type;
        int data_len = address_data_len(addr);
        std::memcpy(key.bytes + 1, &addr->data, static_cast<size_t>(data_len));
        std::memcpy(key.bytes + 17, &addr->port, 2);
        return key;
    }

    FixedHashMap<AddressKey, RateBucket, 4096> buckets_;
};

} // namespace netudp

#endif /* NETUDP_RATE_LIMITER_H */
