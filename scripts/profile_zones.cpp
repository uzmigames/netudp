/**
 * @file profile_zones.cpp
 * @brief Quick profiling zone exerciser — no connect token needed.
 *
 * Directly invokes internal subsystems (crypto, channel, fragment, bandwidth)
 * to collect zone timing data.
 *
 * Build: included in CMake via scripts/ — or compile manually:
 *   cl /std:c++17 /O2 /I include /I src scripts/profile_zones.cpp
 *       build/Release/netudp.lib ws2_32.lib /Fe:profile_zones.exe
 */

#include <netudp/netudp.h>
#include <netudp/netudp_profiling.h>

#include <cstdio>
#include <cstring>

/* -----------------------------------------------------------------------
 * Internal headers (relative paths from project root)
 * --------------------------------------------------------------------- */
#include "src/crypto/packet_crypto.h"
#include "src/crypto/xchacha.h"
#include "src/channel/channel.h"
#include "src/fragment/fragment.h"
#include "src/bandwidth/bandwidth.h"
#include "src/reliability/reliable_channel.h"
#include "src/reliability/packet_tracker.h"
#include "src/reliability/rtt.h"
#include "src/connection/rate_limiter.h"

using namespace netudp;
using namespace netudp::crypto;

static void print_zones() {
    netudp_profile_zone_t zones[64];
    int count = netudp_profiling_get_zones(zones, 64);

    printf("\n%-34s  %8s  %10s  %10s  %10s  %10s\n",
           "zone", "calls", "total_us", "avg_ns", "min_ns", "max_ns");
    printf("%-34s  %8s  %10s  %10s  %10s  %10s\n",
           "----------------------------------", "--------",
           "----------", "----------", "----------", "----------");

    for (int i = 0; i < count; ++i) {
        const auto& z = zones[i];
        if (z.call_count == 0) { continue; }
        double avg_ns   = static_cast<double>(z.total_ns) / static_cast<double>(z.call_count);
        double total_us = static_cast<double>(z.total_ns) / 1000.0;
        printf("%-34s  %8llu  %10.1f  %10.1f  %10llu  %10llu\n",
               z.name,
               (unsigned long long)z.call_count,
               total_us, avg_ns,
               (unsigned long long)z.min_ns,
               (unsigned long long)z.max_ns);
    }
    printf("\n");
}

int main() {
    netudp_init();
    netudp_profiling_enable(1);

    constexpr int N = 10000;

    /* --- 1. Crypto: packet encrypt / decrypt --- */
    {
        KeyEpoch epoch = {};
        epoch.epoch_number = 1;
        epoch.epoch_start_time = 1.0;
        uint8_t key[32] = {};
        for (int i = 0; i < 32; ++i) { key[i] = (uint8_t)(i + 1); }
        std::memcpy(epoch.tx_key, key, 32);
        std::memcpy(epoch.rx_key, key, 32);
        epoch.replay.reset();

        uint8_t pt[64]  = {};
        uint8_t ct[128] = {};
        uint8_t dt[64]  = {};
        for (int i = 0; i < N; ++i) {
            pt[0] = (uint8_t)i;
            int ct_len = packet_encrypt(&epoch, 0xDEAD, 0x03, pt, 64, ct);
            uint64_t nonce = epoch.tx_nonce_counter - 1;
            /* Use fresh rx epoch so replay window starts clean each round */
            KeyEpoch rx = epoch;
            rx.tx_nonce_counter = 0;
            packet_decrypt(&rx, 0xDEAD, 0x03, nonce, ct, ct_len, dt);
        }
        printf("[crypto]      %d encrypt+decrypt cycles done\n", N);
    }

    /* --- 2. Channel: queue_send --- */
    {
        netudp_channel_config_t cfg = {};
        cfg.type     = NETUDP_CHANNEL_UNRELIABLE;
        cfg.priority = 1;
        cfg.weight   = 1;
        cfg.nagle_ms = 0;

        Channel chan;
        chan.init(0, cfg);

        uint8_t data[64] = {};
        for (int i = 0; i < N; ++i) {
            chan.queue_send(data, 64, NETUDP_SEND_UNRELIABLE);
        }
        printf("[channel]     %d queue_send cycles done\n", N);
    }

    /* --- 3. ReliableChannel: find_retransmits --- */
    {
        ReliableChannelState rcs;
        rcs.reset();

        uint8_t data[64] = {};
        for (int i = 0; i < 32; ++i) {
            rcs.record_send(data, 64, (uint16_t)i, 1.0);
        }

        uint16_t seqs[32];
        for (int i = 0; i < N; ++i) {
            rcs.find_retransmits(100.0, 0.1, seqs, 32);
        }
        printf("[reliable]    %d find_retransmits cycles done\n", N);
    }

    /* --- 4. Fragment reassembly --- */
    {
        FragmentReassembler reassembler;
        reassembler.init(64 * 1024);

        constexpr int FRAG_PAYLOAD = 800;
        uint8_t frag_data[FRAG_PAYLOAD] = {};
        int dummy_size = 0;
        int completed  = 0;

        for (int i = 0; i < N; ++i) {
            uint16_t msg_id = (uint16_t)(i % 64);
            /* Send 3 fragments per message; complete on 3rd */
            for (uint8_t fi = 0; fi < 3; ++fi) {
                const uint8_t* res = reassembler.on_fragment_received(
                    msg_id, fi, 3, frag_data, FRAG_PAYLOAD,
                    FRAG_PAYLOAD, (double)i * 0.001, &dummy_size);
                if (res) { completed++; }
            }
        }
        printf("[fragment]    %d reassembly cycles (%d completed)\n", N, completed);
    }

    /* --- 5. Bandwidth bucket: refill + try_consume --- */
    {
        BandwidthBucket bw;
        bw.init(0.0);

        int consumed = 0;
        for (int i = 0; i < N; ++i) {
            bw.refill(static_cast<double>(i) / 60.0);
            if (bw.try_consume(1400)) { consumed++; }
        }
        printf("[bandwidth]   %d refill+consume cycles (%d consumed)\n", N, consumed);
    }

    /* --- 6. Congestion control: evaluate --- */
    {
        CongestionControl cc;
        cc.init();

        for (int i = 0; i < N; ++i) {
            if (i % 20 == 0) { cc.on_packet_lost(); }
            else              { cc.on_packet_acked(); }
            cc.on_rtt_sample();
            cc.evaluate();
        }
        printf("[congestion]  %d evaluate cycles done\n", N);
    }

    /* --- 7. PacketTracker: send + on_recv + build_ack + process_acks --- */
    {
        PacketTracker tracker;
        tracker.reset();

        for (int i = 0; i < N; ++i) {
            uint16_t seq = tracker.send_packet(static_cast<double>(i) * 0.001);
            tracker.on_packet_received(seq, static_cast<double>(i) * 0.001 + 0.01);
            AckFields af = tracker.build_ack_fields(static_cast<double>(i) * 0.001 + 0.02);
            tracker.process_acks(af);
        }
        printf("[pkt_tracker] %d send+recv+ack cycles done\n", N);
    }

    /* --- 8. RTT estimator: on_sample --- */
    {
        RttEstimator rtt;
        rtt.reset();

        for (int i = 0; i < N; ++i) {
            rtt.on_sample(static_cast<double>(i) * 0.001,
                          static_cast<double>(i) * 0.001 + 0.05,
                          500);
        }
        printf("[rtt]         %d on_sample cycles done\n", N);
    }

    /* --- 9. ReliableChannel: record_send + mark_acked + buffer_recv + deliver --- */
    {
        ReliableChannelState rcs;
        rcs.reset();

        uint8_t data[64] = {};
        int delivered_total = 0;
        for (int i = 0; i < N; ++i) {
            uint16_t seq = rcs.send_seq;
            rcs.record_send(data, 64, seq, static_cast<double>(i) * 0.001);
            rcs.mark_acked(seq);
            rcs.buffer_received_ordered(rcs.recv_seq, data, 64);
            delivered_total += rcs.deliver_ordered([](const uint8_t*, int, uint16_t) {});
        }
        printf("[reliable]    %d record+ack+buffer+deliver cycles (%d delivered)\n", N, delivered_total);
    }

    /* --- 11. Rate limiter --- */
    {
        RateLimiter rl;
        netudp_address_t addr = {};
        addr.type   = NETUDP_ADDRESS_IPV4;
        addr.port   = 12345;
        addr.data.ipv4[0] = 1; addr.data.ipv4[1] = 2;
        addr.data.ipv4[2] = 3; addr.data.ipv4[3] = 4;

        int allowed = 0;
        for (int i = 0; i < N; ++i) {
            if (rl.allow(&addr, static_cast<double>(i) / 60.0)) { allowed++; }
        }
        printf("[ratelimit]   %d allow() calls (%d allowed)\n", N, allowed);
    }

    /* --- Results --- */
    printf("\n=== PROFILING ZONE REPORT (%d iterations each) ===", N);
    print_zones();

    netudp_term();
    return 0;
}
