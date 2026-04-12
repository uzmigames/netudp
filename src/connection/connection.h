#ifndef NETUDP_CONNECTION_H
#define NETUDP_CONNECTION_H

/**
 * @file connection.h
 * @brief Server-side connection state. One per connected client slot.
 *        Wires ALL subsystems: crypto, packet tracker, RTT, channels,
 *        reliability, fragmentation, bandwidth, stats.
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

#include <cstdint>
#include <cstring>

namespace netudp {

static constexpr int MAX_CHANNELS_PER_CONNECTION = 32;

/** Delivered message waiting for app to pick up via netudp_server_receive. */
struct DeliveredMessage {
    uint8_t  data[NETUDP_MTU];
    int      size = 0;
    int      channel = 0;
    uint16_t sequence = 0;
    bool     valid = false;
};

struct Connection {
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

    /* Channels */
    Channel channels[MAX_CHANNELS_PER_CONNECTION];
    int     num_channels = 0;

    /* Per-channel reliable state */
    ReliableChannelState reliable_state[MAX_CHANNELS_PER_CONNECTION];

    /* Fragment reassembly */
    FragmentReassembler fragment_reassembler;
    uint16_t fragment_send_id = 0;

    /* Bandwidth control */
    BandwidthBucket bandwidth;
    QueuedBitsBudget budget;
    CongestionControl congestion;

    /* Statistics */
    ConnectionStats stats;

    /* Delivered message queue (ring buffer for app pickup) */
    FixedRingBuffer<DeliveredMessage, 256> delivered_messages;

    /* Timing */
    double   connect_time = 0.0;
    double   last_recv_time = 0.0;
    double   last_send_time = 0.0;
    double   last_keepalive_time = 0.0;
    uint32_t timeout_seconds = 10;

    void init_subsystems(const netudp_channel_config_t* channel_configs, int n_channels, double time) {
        num_channels = (n_channels > MAX_CHANNELS_PER_CONNECTION) ? MAX_CHANNELS_PER_CONNECTION : n_channels;
        for (int i = 0; i < num_channels; ++i) {
            channels[i].init(i, channel_configs[i]);
            reliable_state[i].reset();
        }
        packet_tracker.reset();
        rtt.reset();
        fragment_reassembler.init(NETUDP_MAX_MESSAGE_SIZE);
        bandwidth.init(time);
        congestion.init();
        stats = ConnectionStats{};
        stats.last_throughput_time_ = time;
        delivered_messages.clear();
        connect_time = time;
        last_recv_time = time;
        last_send_time = time;
        last_keepalive_time = time;
    }

    void reset() {
        active = false;
        generation++;
        std::memset(&address, 0, sizeof(address));
        client_id = 0;
        std::memset(user_data, 0, sizeof(user_data));
        key_epoch = crypto::KeyEpoch{};
        packet_tracker.reset();
        rtt.reset();
        for (int i = 0; i < num_channels; ++i) {
            reliable_state[i].reset();
        }
        fragment_reassembler.destroy();
        congestion.reset();
        stats = ConnectionStats{};
        delivered_messages.clear();
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
