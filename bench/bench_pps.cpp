/**
 * @file bench_pps.cpp
 * @brief Packets-per-second loopback benchmark.
 *
 * Measures steady-state encrypted UDP throughput: 1 client → server,
 * unreliable channel, 64-byte payload.  Server counts arrivals via a
 * packet handler; PPS = arrivals / elapsed_seconds.
 */

#include "bench_main.h"
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

static constexpr uint64_t kPpsProtocolId  = 0xBEEF00010000001ULL;
static constexpr uint16_t kPpsBasePort    = 29000U;
static constexpr int      kPpsPayloadSize = 64;
static constexpr uint8_t  kPpsPacketType  = 0x01U;
static constexpr double   kBenchSec       = 2.0;  /* measurement window */
static constexpr int      kBatchPerLoop   = 16;   /* sends per update() cycle */

static const uint8_t kPpsKey[32] = {
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
    0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x01,
    0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x71, 0x81,
    0x91, 0xA1, 0xB1, 0xC1, 0xD1, 0xE1, 0xF1, 0x02,
};

struct PpsCounter { uint64_t received = 0; };

static void pps_handler(void* ctx, int /*ci*/, const void* /*d*/, int /*s*/, int /*ch*/) {
    static_cast<PpsCounter*>(ctx)->received++;
}

static BenchResult run_pps_bench(const BenchConfig& cfg, int port_offset) {
    using Clock = std::chrono::high_resolution_clock;

    BenchResult r;
    r.name = "pps_1client";

    char srv_addr[64];
    std::snprintf(srv_addr, sizeof(srv_addr), "127.0.0.1:%u",
                  static_cast<unsigned>(kPpsBasePort + port_offset));

    /* Server — must configure ≥1 channel or frame parser drops all messages */
    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id = kPpsProtocolId;
    std::memcpy(srv_cfg.private_key, kPpsKey, 32);
    srv_cfg.num_channels = 1;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    double sim_time = 5000.0;
    netudp_server_t* server = netudp_server_create(srv_addr, &srv_cfg, sim_time);
    if (server == nullptr) {
        std::fprintf(stderr, "[pps] server_create failed at %s\n", srv_addr);
        return r;
    }
    netudp_server_start(server, 8);

    PpsCounter counter;
    netudp_server_set_packet_handler(server, kPpsPacketType, pps_handler, &counter);

    /* Connect token */
    const char* addrs[] = { srv_addr };
    uint8_t token[2048] = {};
    if (netudp_generate_connect_token(1, addrs, 300, 10,
                                      9001ULL, kPpsProtocolId,
                                      kPpsKey, nullptr, token) != NETUDP_OK) {
        netudp_server_stop(server);
        netudp_server_destroy(server);
        return r;
    }

    /* Client — channel config must match server */
    netudp_client_config_t cli_cfg = {};
    cli_cfg.protocol_id = kPpsProtocolId;
    cli_cfg.num_channels = 1;
    cli_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
    netudp_client_t* client = netudp_client_create(nullptr, &cli_cfg, sim_time);
    if (client == nullptr) {
        netudp_server_stop(server);
        netudp_server_destroy(server);
        return r;
    }
    netudp_client_connect(client, token);

    /* Handshake — allow up to 3 seconds */
    auto handshake_end = Clock::now() + std::chrono::milliseconds(3000);
    while (Clock::now() < handshake_end && netudp_client_state(client) != 3) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        netudp_client_update(client, sim_time);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (netudp_client_state(client) != 3) {
        int final_state = netudp_client_state(client);
        int srv_clients = netudp_server_max_clients(server);
        std::fprintf(stderr, "[pps] client failed to connect (state=%d, srv_max=%d)\n",
                     final_state, srv_clients);
        netudp_client_disconnect(client);
        netudp_client_destroy(client);
        netudp_server_stop(server);
        netudp_server_destroy(server);
        return r;
    }

    /* Build payload: first byte = packet type */
    uint8_t payload[kPpsPayloadSize] = {};
    payload[0] = kPpsPacketType;

    /* Rate limiter design: 60 pps, burst=10.  We advance sim_time by
     * 1/60 s per send so the limiter always has exactly 1 token.
     * This bypasses the anti-DDoS constraint and measures real
     * socket + AEAD throughput as the benchmark intent requires.    */
    static constexpr double kSimStepPerPacket = 1.0 / 60.0; /* 1 token refilled */

    /* Warm-up: 10 sends, drain connection state */
    for (int i = 0; i < 10; ++i) {
        sim_time += kSimStepPerPacket;
        netudp_client_send(client, 0, payload, kPpsPayloadSize, NETUDP_SEND_UNRELIABLE);
        netudp_client_update(client, sim_time);
        netudp_server_update(server, sim_time);
        netudp_message_t* msgs[64];
        int n = netudp_server_receive(server, 0, msgs, 64);
        for (int m = 0; m < n; ++m) { netudp_message_release(msgs[m]); }
    }
    counter.received = 0;

    /* Benchmark: `outer` send/recv pairs; each pair is one timing sample.
     * We send 1 packet per sample to keep the rate limiter happy. */
    const int outer = (cfg.measure_iters > 1) ? cfg.measure_iters : 200;
    r.samples_ns.reserve(static_cast<size_t>(outer));

    uint64_t total_sent     = 0;
    uint64_t total_received = 0;

    auto bench_t0 = Clock::now();

    for (int s = 0; s < outer; ++s) {
        auto t0 = Clock::now();

        sim_time += kSimStepPerPacket;
        netudp_client_send(client, 0, payload, kPpsPayloadSize, NETUDP_SEND_UNRELIABLE);
        total_sent++;

        netudp_client_update(client, sim_time);
        netudp_server_update(server, sim_time);

        netudp_message_t* msgs[64];
        int n = netudp_server_receive(server, 0, msgs, 64);
        for (int m = 0; m < n; ++m) { netudp_message_release(msgs[m]); }

        auto t1 = Clock::now();
        double loop_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        r.samples_ns.push_back(loop_ns);
    }

    /* Extra drain — some packets might be in flight */
    for (int i = 0; i < 16; ++i) {
        sim_time += 0.001;
        netudp_server_update(server, sim_time);
        netudp_client_update(client, sim_time);
        netudp_message_t* msgs[64];
        int n = netudp_server_receive(server, 0, msgs, 64);
        for (int m = 0; m < n; ++m) { netudp_message_release(msgs[m]); }
    }
    total_received = counter.received;

    double elapsed = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - bench_t0).count()) / 1e6;

    r.ops_per_sec = (elapsed > 0.0)
                  ? static_cast<double>(total_received) / elapsed
                  : 0.0;

    std::printf("          sent=%llu  received=%llu  elapsed=%.3fs\n",
                static_cast<unsigned long long>(total_sent),
                static_cast<unsigned long long>(total_received),
                elapsed);

    /* Cleanup */
    netudp_client_disconnect(client);
    netudp_client_destroy(client);
    netudp_server_stop(server);
    netudp_server_destroy(server);

    return r;
}

void register_pps_bench(BenchRegistry& reg) {
    reg.add("pps_1client", [](const BenchConfig& cfg) -> BenchResult {
        return run_pps_bench(cfg, 0);
    });
}
