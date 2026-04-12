#ifndef NETUDP_NETWORK_SIM_H
#define NETUDP_NETWORK_SIM_H

#include <netudp/netudp_types.h>
#include <cstring>
#include <cstdint>

namespace netudp {

/** Configuration parameters for the network simulator. */
struct NetSimConfig {
    float latency_min_ms    = 0.0F;
    float latency_max_ms    = 0.0F;
    float jitter_ms         = 0.0F;
    float loss_percent      = 0.0F;   /* 0-100 */
    float duplicate_percent = 0.0F;   /* 0-100 */
    float reorder_percent   = 0.0F;   /* 0-100 */
    float incoming_lag_ms   = 0.0F;
};

/** A single packet held in the simulator queue. */
struct SimPacket {
    uint8_t          data[1500];
    int              len;
    netudp_address_t from;
    double           deliver_time;
};

/**
 * NetworkSimulator — a zero-allocation, ring-buffer based network
 * conditions emulator.  Supports latency, jitter, packet loss,
 * duplication, and reordering.
 */
struct NetworkSimulator {
    NetSimConfig config;
    SimPacket    queue[512];
    int          head;       /* ring buffer read index  */
    int          tail;       /* ring buffer write index */
    int          count;
    uint32_t     rng_state;  /* xorshift32 seed         */

    /** Initialise the simulator with the given config and optional RNG seed. */
    void init(const NetSimConfig& cfg, uint32_t seed = 12345);

    /** Reset the ring buffer (does not clear config). */
    void reset();

    /**
     * Submit one incoming packet through the simulator.
     * Returns the number of packets actually inserted (0 = dropped,
     * 1 = normal, 2 = duplicated).
     */
    int submit(const uint8_t* data, int len,
               const netudp_address_t* from, double current_time);

    /**
     * Deliver all buffered packets whose deliver_time <= current_time.
     * Callback signature: void(void* ctx, const uint8_t* data, int len,
     *                         const netudp_address_t* from)
     */
    void poll(double current_time, void* ctx,
              void (*callback)(void* ctx, const uint8_t* data, int len,
                               const netudp_address_t* from));

private:
    uint32_t rand_u32();
    float    rand_float();   /* returns value in [0, 100) */
    void     insert(const uint8_t* data, int len,
                    const netudp_address_t* from, double deliver_time);
};

} /* namespace netudp */

#endif /* NETUDP_NETWORK_SIM_H */
