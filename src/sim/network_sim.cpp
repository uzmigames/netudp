#include "network_sim.h"

#include <algorithm>
#include <cstring>

namespace netudp {

/* ======================================================================
 * Private helpers
 * ====================================================================== */

uint32_t NetworkSimulator::rand_u32() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* Returns a value in [0, 100). */
float NetworkSimulator::rand_float() {
    return static_cast<float>(rand_u32() % 10000) / 100.0F;
}

void NetworkSimulator::insert(const uint8_t* data, int len,
                              const netudp_address_t* from, double deliver_time) {
    if (count >= 512) {
        /* Queue full — drop */
        return;
    }

    SimPacket& p = queue[tail];
    int copy_len = len < 1500 ? len : 1500;
    memcpy(p.data, data, static_cast<size_t>(copy_len));
    p.len          = copy_len;
    p.from         = *from;
    p.deliver_time = deliver_time;

    tail = (tail + 1) % 512;
    ++count;
}

/* ======================================================================
 * Public interface
 * ====================================================================== */

void NetworkSimulator::init(const NetSimConfig& cfg, uint32_t seed) {
    config    = cfg;
    head      = 0;
    tail      = 0;
    count     = 0;
    rng_state = seed;
}

void NetworkSimulator::reset() {
    head  = 0;
    tail  = 0;
    count = 0;
}

int NetworkSimulator::submit(const uint8_t* data, int len,
                             const netudp_address_t* from, double current_time) {
    /* --- 1. Loss --- */
    if (rand_float() < config.loss_percent) {
        return 0;
    }

    /* Compute base delivery delay (ms → seconds). */
    float range_ms = config.latency_max_ms - config.latency_min_ms;
    float delay_ms = config.latency_min_ms
                     + ((rand_float() / 100.0F) * range_ms);

    /* Add jitter: random value in [-jitter_ms, +jitter_ms]. */
    float jitter_offset = (((rand_float() / 100.0F) * config.jitter_ms) * 2.0F)
                          - config.jitter_ms;
    delay_ms += jitter_offset;
    delay_ms = std::max(delay_ms, 0.0F);

    double deliver_time = current_time + (static_cast<double>(delay_ms) / 1000.0);

    /* --- 2. Duplicate --- */
    if (rand_float() < config.duplicate_percent) {
        insert(data, len, from, deliver_time);
        insert(data, len, from, deliver_time);
        return 2;
    }

    /* --- 3. Reorder --- */
    if (rand_float() < config.reorder_percent) {
        /* Add extra 0–50 ms beyond the normal delay. */
        float extra_ms = (rand_float() / 100.0F) * 50.0F;
        deliver_time  += static_cast<double>(extra_ms) / 1000.0;
    }

    /* --- 4. Normal insert --- */
    insert(data, len, from, deliver_time);
    return 1;
}

void NetworkSimulator::poll(double current_time, void* ctx,
                            void (*callback)(void* ctx, const uint8_t* data,
                                             int len,
                                             const netudp_address_t* from)) {
    /* Scan all entries in-place: deliver due packets, compact survivors.
     * Two-pass: first deliver, then compact without a temp buffer.      */
    int write = 0;
    for (int n = 0; n < count; ++n) {
        int idx = (head + n) % 512;
        if (queue[idx].deliver_time <= current_time) {
            callback(ctx, queue[idx].data, queue[idx].len, &queue[idx].from);
        } else {
            if (write != idx) {
                queue[write] = queue[idx];
            }
            ++write;
        }
    }
    head  = 0;
    tail  = write % 512;
    count = write;
}

} /* namespace netudp */
