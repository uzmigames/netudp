#ifndef NETUDP_CONNECTION_H
#define NETUDP_CONNECTION_H

/**
 * @file connection.h
 * @brief Server-side connection state. One per connected client slot.
 *        Wires ALL subsystems: crypto, packet tracker, RTT, channels,
 *        reliability, fragmentation, bandwidth, stats.
 *
 * Design: heavy per-connection buffers (channels, reliable_state,
 * fragment_reassembler, delivered_messages) are heap-allocated only when a
 * client actually connects (init_subsystems). Idle slots stay < 3 KB each,
 * allowing 1024 slots to fit well within 100 MB.
 */

#include <netudp/netudp_types.h>
#include <netudp/netudp_config.h>
#include "../crypto/packet_crypto.h"
#include "../reliability/packet_tracker.h"
#include "../reliability/rtt.h"
#include "../reliability/reliable_channel.h"
#include "../channel/channel.h"
#include "../fragment/fragment.h"
#include "../bandwidth/bandwidth.h"
#include "../stats/stats.h"
#include "connect_token.h"

#include "../core/log.h"
#include "../profiling/profiler.h"
#include <cstdint>
#include <cstring>
#include <new>

namespace netudp {

static constexpr int MAX_CHANNELS_PER_CONNECTION = 4;

/** Delivered message waiting for app to pick up via netudp_server_receive. */
struct DeliveredMessage {
    uint8_t  data[NETUDP_MTU];
    int      size = 0;
    int      channel = 0;
    uint16_t sequence = 0;
    bool     valid = false;
};

/**
 * Heavy per-connection state — heap-allocated on connect, freed on disconnect.
 * Keeping this off the slot array means idle slots cost ~3 KB each instead of
 * ~2.1 MB each.
 */
struct ConnectionData {
    Channel channels[MAX_CHANNELS_PER_CONNECTION];
    ReliableChannelState reliable_state[MAX_CHANNELS_PER_CONNECTION];
    FragmentReassembler fragment_reassembler;
    FixedRingBuffer<DeliveredMessage, 256> delivered_messages;
};

struct alignas(64) Connection {
    bool     active = false;
    uint32_t generation = 0;

    netudp_address_t address = {};
    uint64_t         client_id = 0;
    uint8_t          user_data[256] = {};

    /* Crypto */
    crypto::KeyEpoch key_epoch;

    /* Packet-level ack tracking */
    PacketTracker packet_tracker;

    /* RTT estimation */
    RttEstimator rtt;

    /* Channel count (set in init_subsystems) */
    int num_channels = 0;

    /* Heavy data — null for idle slots, heap-allocated for active connections */
    ConnectionData* cdata = nullptr;

    /* Fragment send id (lightweight, stays here) */
    uint16_t fragment_send_id = 0;

    /* Bandwidth control */
    BandwidthBucket bandwidth;
    QueuedBitsBudget budget;
    CongestionControl congestion;

    /* Statistics */
    ConnectionStats stats;

    /* Timing */
    double   connect_time = 0.0;
    double   last_recv_time = 0.0;
    double   last_send_time = 0.0;
    double   last_keepalive_time = 0.0;
    double   next_slow_tick = 0.0;  /* Amortized cleanup/stats (phase 34) */
    uint32_t timeout_seconds = 10;
    uint8_t  pending_mask = 0;      /* 1 bit per channel with pending sends (phase 33) */
    uint16_t slot_id = 0xFFFF;     /* Slot index embedded in wire header (phase 36). 0xFFFF = unassigned. */

    /* Cached sockaddr for zero-copy sends — avoids per-send memset(128) + field copy.
     * Populated once in server_handle_connection_request, reused on every send. (phase 38) */
    alignas(8) uint8_t cached_sa[128] = {};  /* sizeof(sockaddr_storage) on all platforms */
    int cached_sa_len = 0;

    /* --- Convenience accessors (only valid when cdata != nullptr) --- */
    Channel& ch(int i)                   { return cdata->channels[i]; }
    const Channel& ch(int i) const       { return cdata->channels[i]; }
    ReliableChannelState& rs(int i)      { return cdata->reliable_state[i]; }
    FragmentReassembler& frag()          { return cdata->fragment_reassembler; }
    FixedRingBuffer<DeliveredMessage, 256>& delivered() { return cdata->delivered_messages; }

    void init_subsystems(const netudp_channel_config_t* channel_configs, int n_channels, double time) {
        NETUDP_ZONE("conn::init");
        /* Allocate heavy data for this active connection */
        cdata = new (std::nothrow) ConnectionData();
        if (cdata == nullptr) {
            return; /* Out of memory — connection will be non-functional */
        }

        num_channels = (n_channels > MAX_CHANNELS_PER_CONNECTION) ? MAX_CHANNELS_PER_CONNECTION : n_channels;
        for (int i = 0; i < num_channels; ++i) {
            cdata->channels[i].init(i, channel_configs[i]);
            cdata->reliable_state[i].reset();
        }
        packet_tracker.reset();
        rtt.reset();
        cdata->fragment_reassembler.init(NETUDP_MAX_MESSAGE_SIZE);
        bandwidth.init(time);
        congestion.init();
        stats = ConnectionStats{};
        stats.last_throughput_time_ = time;
        cdata->delivered_messages.clear();
        connect_time = time;
        last_recv_time = time;
        last_send_time = time;
        last_keepalive_time = time;
    }

    void reset() {
        NETUDP_ZONE("conn::reset");
        active = false;
        generation++;
        std::memset(&address, 0, sizeof(address));
        client_id = 0;
        std::memset(user_data, 0, sizeof(user_data));
        key_epoch = crypto::KeyEpoch{};
        packet_tracker.reset();
        rtt.reset();

        /* Free heavy data if allocated */
        if (cdata != nullptr) {
            cdata->fragment_reassembler.destroy();
            delete cdata;
            cdata = nullptr;
        }

        congestion.reset();
        std::memset(cached_sa, 0, sizeof(cached_sa));
        cached_sa_len = 0;
        slot_id = 0xFFFF;
        stats = ConnectionStats{};
        connect_time = 0.0;
        last_recv_time = 0.0;
        last_send_time = 0.0;
        last_keepalive_time = 0.0;
        timeout_seconds = 10;
        num_channels = 0;
        fragment_send_id = 0;
    }
};

} // namespace netudp

#endif /* NETUDP_CONNECTION_H */
