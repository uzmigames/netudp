/**
 * @file bench_scalability.cpp
 * @brief PPS vs connection count — measures how throughput scales with clients.
 *
 * Runs the PPS benchmark with 1, 4, and 16 simultaneous clients.
 * Each run uses a distinct server port to avoid bind conflicts.
 */

#include "bench_main.h"
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

static constexpr uint64_t kScaleProtocolId = 0xBEEF00030000003ULL;
static constexpr uint16_t kScaleBasePort   = 29200U;
static constexpr uint8_t  kScaleType       = 0x03U;
static constexpr int      kScalePayload    = 64;
static constexpr int      kBatch           = 8;  /* sends per client per cycle */

static const uint8_t kScaleKey[32] = {
    0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0,
    0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x01, 0x11, 0x21,
    0x31, 0x41, 0x51, 0x61, 0x71, 0x81, 0x91, 0xA1,
    0xB1, 0xC1, 0xD1, 0xE1, 0xF1, 0x02, 0x12, 0x22,
};

struct ScaleCounter { uint64_t received = 0; };

static void scale_handler(void* ctx, int, const void*, int, int) {
    static_cast<ScaleCounter*>(ctx)->received++;
}

static BenchResult run_scale(const BenchConfig& cfg, int num_clients, int port_offset) {
    using Clock = std::chrono::high_resolution_clock;

    BenchResult r;
    char name_buf[32];
    std::snprintf(name_buf, sizeof(name_buf), "scale_%dc", num_clients);
    r.name = name_buf;

    char srv_addr[64];
    std::snprintf(srv_addr, sizeof(srv_addr), "127.0.0.1:%u",
                  static_cast<unsigned>(kScaleBasePort + port_offset));

    /* Server — must configure ≥1 channel or frame parser drops all messages */
    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id = kScaleProtocolId;
    std::memcpy(srv_cfg.private_key, kScaleKey, 32);
    srv_cfg.num_channels = 1;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    double sim_time = 7000.0;
    netudp_server_t* server = netudp_server_create(srv_addr, &srv_cfg, sim_time);
    if (server == nullptr) {
        std::fprintf(stderr, "[scale] server_create failed (%d clients)\n", num_clients);
        return r;
    }
    netudp_server_start(server, num_clients + 4);

    ScaleCounter counter;
    netudp_server_set_packet_handler(server, kScaleType, scale_handler, &counter);

    /* Connect all clients */
    const char* addrs[] = { srv_addr };
    std::vector<netudp_client_t*> clients;
    clients.reserve(static_cast<size_t>(num_clients));

    for (int c = 0; c < num_clients; ++c) {
        uint8_t token[2048] = {};
        if (netudp_generate_connect_token(1, addrs, 300, 10,
                                          static_cast<uint64_t>(7000 + c),
                                          kScaleProtocolId,
                                          kScaleKey, nullptr, token) != NETUDP_OK) {
            break;
        }
        netudp_client_config_t cli_cfg = {};
        cli_cfg.protocol_id = kScaleProtocolId;
        cli_cfg.num_channels = 1;
        cli_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
        netudp_client_t* client = netudp_client_create(nullptr, &cli_cfg, sim_time);
        if (client == nullptr) { break; }
        netudp_client_connect(client, token);
        clients.push_back(client);
    }

    /* Handshake — allow up to 5 seconds for all clients */
    auto hs_deadline = Clock::now() + std::chrono::milliseconds(5000);
    while (Clock::now() < hs_deadline) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        for (auto* c : clients) { netudp_client_update(c, sim_time); }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        bool all_ok = true;
        for (auto* c : clients) {
            if (netudp_client_state(c) != 3) { all_ok = false; break; }
        }
        if (all_ok) break;
    }

    int connected = 0;
    for (auto* c : clients) {
        if (netudp_client_state(c) == 3) { connected++; }
    }

    if (connected == 0) {
        std::fprintf(stderr, "[scale] no clients connected\n");
        for (auto* c : clients) { netudp_client_disconnect(c); netudp_client_destroy(c); }
        netudp_server_stop(server);
        netudp_server_destroy(server);
        return r;
    }

    /* Payload */
    uint8_t payload[kScalePayload] = {};
    payload[0] = kScaleType;

    /* Rate limiter: 60 pps per IP. Each client is a distinct source IP on
     * Windows (same loopback address, different ephemeral port). The limiter
     * buckets by IP, so all clients from 127.0.0.1 share the same 60-pps
     * budget. Advance sim_time by 1/60 s per send to keep bucket full.   */
    static constexpr double kSimStep = 1.0 / 60.0;

    /* Warm-up: 1 send per client × 8 iterations */
    counter.received = 0;
    for (int i = 0; i < 8; ++i) {
        sim_time += kSimStep;
        for (auto* c : clients) {
            if (netudp_client_state(c) == 3) {
                netudp_client_send(c, 0, payload, kScalePayload, NETUDP_SEND_UNRELIABLE);
            }
        }
        for (auto* c : clients) { netudp_client_update(c, sim_time); }
        netudp_server_update(server, sim_time);
        netudp_message_t* msgs[64];
        for (int ci = 0; ci < num_clients + 4; ++ci) {
            int n = netudp_server_receive(server, ci, msgs, 64);
            for (int m = 0; m < n; ++m) { netudp_message_release(msgs[m]); }
        }
    }
    counter.received = 0;

    /* Benchmark: send 1 packet per client per sample */
    const int outer = (cfg.measure_iters > 1) ? cfg.measure_iters : 50;
    r.samples_ns.reserve(static_cast<size_t>(outer));

    auto bench_t0 = Clock::now();
    uint64_t total_sent = 0;

    for (int s = 0; s < outer; ++s) {
        auto t0 = Clock::now();

        sim_time += kSimStep;
        for (auto* c : clients) {
            if (netudp_client_state(c) == 3) {
                netudp_client_send(c, 0, payload, kScalePayload, NETUDP_SEND_UNRELIABLE);
                total_sent++;
            }
        }

        for (auto* c : clients) { netudp_client_update(c, sim_time); }
        netudp_server_update(server, sim_time);

        netudp_message_t* msgs[128];
        for (int ci = 0; ci < num_clients + 4; ++ci) {
            int n = netudp_server_receive(server, ci, msgs, 128);
            for (int m = 0; m < n; ++m) { netudp_message_release(msgs[m]); }
        }

        auto t1 = Clock::now();
        double loop_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        r.samples_ns.push_back(
            (connected > 0) ? (loop_ns / static_cast<double>(connected)) : loop_ns);
    }

    /* Drain */
    for (int i = 0; i < 16; ++i) {
        sim_time += 0.001;
        netudp_server_update(server, sim_time);
        for (auto* c : clients) { netudp_client_update(c, sim_time); }
        netudp_message_t* msgs[128];
        for (int ci = 0; ci < num_clients + 4; ++ci) {
            int n = netudp_server_receive(server, ci, msgs, 128);
            for (int m = 0; m < n; ++m) { netudp_message_release(msgs[m]); }
        }
    }

    double elapsed = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - bench_t0).count()) / 1e6;

    r.ops_per_sec = (elapsed > 0.0)
                  ? static_cast<double>(counter.received) / elapsed
                  : 0.0;

    std::printf("          clients=%d/%d  sent=%llu  received=%llu  elapsed=%.3fs\n",
                connected, num_clients,
                static_cast<unsigned long long>(total_sent),
                static_cast<unsigned long long>(counter.received),
                elapsed);

    /* Cleanup */
    for (auto* c : clients) { netudp_client_disconnect(c); netudp_client_destroy(c); }
    netudp_server_stop(server);
    netudp_server_destroy(server);

    return r;
}

void register_scalability_bench(BenchRegistry& reg) {
    reg.add("scale_1client",  [](const BenchConfig& cfg) -> BenchResult {
        return run_scale(cfg, 1,  0);
    });
    reg.add("scale_4clients", [](const BenchConfig& cfg) -> BenchResult {
        return run_scale(cfg, 4,  4);
    });
    reg.add("scale_16clients",[](const BenchConfig& cfg) -> BenchResult {
        return run_scale(cfg, 16, 16);
    });
}
