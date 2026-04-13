/**
 * @file bench_coalescing.cpp
 * @brief Frame coalescing benchmark: measures PPS with N messages per tick.
 *
 * Sends 1, 5, 10, 20 small messages per update cycle to a single client.
 * With coalescing, multiple messages pack into fewer UDP packets, reducing
 * syscall and crypto overhead proportionally.
 */

#include "bench_main.h"
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

static constexpr uint64_t kCoalProtoId   = 0xC0A100020000002ULL;
static constexpr uint16_t kCoalBasePort  = 29500U;
static constexpr int      kCoalMsgSize   = 20; /* Small position-like updates */
static constexpr double   kCoalBenchSec  = 2.0;

static const uint8_t kCoalKey[32] = {
    0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8,
    0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8,
    0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8,
    0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
};

static BenchResult run_coalescing(const BenchConfig& cfg, int msgs_per_tick, int port_offset) {
    using Clock = std::chrono::high_resolution_clock;

    BenchResult r;
    char name_buf[64];
    std::snprintf(name_buf, sizeof(name_buf), "coalesce_%dmsg", msgs_per_tick);
    r.name = name_buf;

    char srv_addr[64];
    std::snprintf(srv_addr, sizeof(srv_addr), "127.0.0.1:%u",
                  static_cast<unsigned>(kCoalBasePort + port_offset));

    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id = kCoalProtoId;
    std::memcpy(srv_cfg.private_key, kCoalKey, 32);
    srv_cfg.num_channels = 1;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    double sim_time = 8000.0;
    netudp_server_t* server = netudp_server_create(srv_addr, &srv_cfg, sim_time);
    if (server == nullptr) { return r; }
    netudp_server_start(server, 4);

    /* Connect client */
    const char* addrs[] = { srv_addr };
    uint8_t token[2048] = {};
    netudp_generate_connect_token(1, addrs, 300, 10, 99001,
                                  kCoalProtoId, kCoalKey, nullptr, token);

    netudp_client_config_t cli_cfg = {};
    cli_cfg.protocol_id = kCoalProtoId;
    cli_cfg.num_channels = 1;
    cli_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    netudp_client_t* client = netudp_client_create(nullptr, &cli_cfg, sim_time);
    if (client == nullptr) {
        netudp_server_destroy(server);
        return r;
    }
    netudp_client_connect(client, token);

    /* Handshake */
    auto deadline = Clock::now() + std::chrono::milliseconds(3000);
    while (Clock::now() < deadline && netudp_client_state(client) != 3) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        netudp_client_update(client, sim_time);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (netudp_client_state(client) != 3) {
        std::fprintf(stderr, "[coalesce_%d] handshake failed (state=%d)\n",
                     msgs_per_tick, netudp_client_state(client));
        netudp_client_destroy(client);
        netudp_server_destroy(server);
        return r;
    }

    /* Prepare message payload */
    uint8_t msg[kCoalMsgSize];
    msg[0] = 0x01; /* packet type for handler dispatch */
    std::memset(msg + 1, 0xAA, kCoalMsgSize - 1);

    /* Measure */
    for (int run = 0; run < cfg.measure_iters; ++run) {
        uint64_t sent = 0;
        uint64_t received = 0;

        auto t0 = Clock::now();
        auto end_time = t0 + std::chrono::milliseconds(static_cast<int>(kCoalBenchSec * 1000));

        /* Simulate game ticks at ~1000 Hz — each tick queues N msgs then updates */
        while (Clock::now() < end_time) {
            sim_time += 0.001; /* 1ms per tick = 1000 Hz */

            /* Queue N messages in one tick */
            for (int m = 0; m < msgs_per_tick; ++m) {
                int rc = netudp_client_send(client, 0, msg, kCoalMsgSize, NETUDP_SEND_NO_NAGLE);
                if (rc == NETUDP_OK) { ++sent; }
            }

            /* Update: client flushes coalesced packet, server receives */
            netudp_client_update(client, sim_time);
            netudp_server_update(server, sim_time);

            /* Drain server receive queue */
            netudp_message_t* msgs_out[64];
            int n = netudp_server_receive_batch(server, msgs_out, 64);
            for (int j = 0; j < n; ++j) {
                ++received;
                netudp_message_release(msgs_out[j]);
            }

            /* Pace to ~1ms per tick for realistic timing */
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }

        /* Drain remaining */
        for (int drain = 0; drain < 100; ++drain) {
            sim_time += 0.001;
            netudp_client_update(client, sim_time);
            netudp_server_update(server, sim_time);
            netudp_message_t* msgs_out[64];
            int n = netudp_server_receive_batch(server, msgs_out, 64);
            for (int j = 0; j < n; ++j) {
                ++received;
                netudp_message_release(msgs_out[j]);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        auto t1 = Clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        double msgs_per_sec = static_cast<double>(received) / elapsed;

        std::printf("          %d msgs/tick: sent=%llu  received=%llu  elapsed=%.3fs  msgs/s=%.0f\n",
                    msgs_per_tick,
                    static_cast<unsigned long long>(sent),
                    static_cast<unsigned long long>(received),
                    elapsed, msgs_per_sec);

        r.samples_ns.push_back(elapsed * 1e9 / static_cast<double>(received > 0 ? received : 1));
        r.ops_per_sec = msgs_per_sec;
    }

    netudp_client_destroy(client);
    netudp_server_destroy(server);
    return r;
}

void register_coalescing_bench(BenchRegistry& reg) {
    reg.add("coalesce_1msg",  [](const BenchConfig& c) { return run_coalescing(c, 1, 0); });
    reg.add("coalesce_5msg",  [](const BenchConfig& c) { return run_coalescing(c, 5, 1); });
    reg.add("coalesce_10msg", [](const BenchConfig& c) { return run_coalescing(c, 10, 2); });
    reg.add("coalesce_20msg", [](const BenchConfig& c) { return run_coalescing(c, 20, 3); });
}
