/**
 * @file bench_latency.cpp
 * @brief End-to-end round-trip latency histogram benchmark.
 *
 * Client sends a ping; server echoes it back; client measures RTT.
 * samples_ns[i] = round-trip latency in nanoseconds for ping i.
 * One-way latency ≈ p99_ns / 2.
 */

#include "bench_main.h"
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

static constexpr uint64_t kLatProtocolId = 0xBEEF00020000002ULL;
static constexpr uint16_t kLatPort       = 29100U;
static constexpr uint8_t  kEchoType      = 0x02U;
static constexpr int      kPayload       = 64;

static const uint8_t kLatKey[32] = {
    0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90,
    0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x01, 0x11,
    0x21, 0x31, 0x41, 0x51, 0x61, 0x71, 0x81, 0x91,
    0xA1, 0xB1, 0xC1, 0xD1, 0xE1, 0xF1, 0x02, 0x12,
};

struct EchoCtx {
    netudp_server_t* server;
};

static void echo_handler(void* ctx, int ci, const void* data, int size, int channel) {
    EchoCtx* e = static_cast<EchoCtx*>(ctx);
    netudp_server_send(e->server, ci, channel, data, size, NETUDP_SEND_UNRELIABLE);
}

static BenchResult run_latency_bench(const BenchConfig& cfg) {
    using Clock = std::chrono::high_resolution_clock;

    BenchResult r;
    r.name = "latency_rtt";

    char srv_addr[64];
    std::snprintf(srv_addr, sizeof(srv_addr), "127.0.0.1:%u",
                  static_cast<unsigned>(kLatPort));

    /* Server — must configure ≥1 channel or frame parser drops all messages */
    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id = kLatProtocolId;
    std::memcpy(srv_cfg.private_key, kLatKey, 32);
    srv_cfg.num_channels = 1;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    double sim_time = 6000.0;
    netudp_server_t* server = netudp_server_create(srv_addr, &srv_cfg, sim_time);
    if (server == nullptr) {
        std::fprintf(stderr, "[latency] server_create failed at %s\n", srv_addr);
        return r;
    }
    netudp_server_start(server, 4);

    EchoCtx echo_ctx = { server };
    netudp_server_set_packet_handler(server, kEchoType, echo_handler, &echo_ctx);

    /* Connect token */
    const char* addrs[] = { srv_addr };
    uint8_t token[2048] = {};
    if (netudp_generate_connect_token(1, addrs, 300, 10,
                                      8001ULL, kLatProtocolId,
                                      kLatKey, nullptr, token) != NETUDP_OK) {
        netudp_server_stop(server);
        netudp_server_destroy(server);
        return r;
    }

    /* Client — channel config must match server */
    netudp_client_config_t cli_cfg = {};
    cli_cfg.protocol_id = kLatProtocolId;
    cli_cfg.num_channels = 1;
    cli_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
    netudp_client_t* client = netudp_client_create(nullptr, &cli_cfg, sim_time);
    if (client == nullptr) {
        netudp_server_stop(server);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (netudp_client_state(client) != 3) {
        std::fprintf(stderr, "[latency] client failed to connect\n");
        netudp_client_disconnect(client);
        netudp_client_destroy(client);
        netudp_server_stop(server);
        netudp_server_destroy(server);
        return r;
    }

    /* Payload: [type=kEchoType][...padding...] */
    uint8_t payload[kPayload] = {};
    payload[0] = kEchoType;

    /* Rate limiter: 60 pps, burst=10. Advance sim_time by 1/60 s per
     * round-trip (server also receives one packet per RTT).          */
    static constexpr double kSimPerRTT = 1.0 / 60.0;

    /* Warm-up: 8 round-trips */
    for (int i = 0; i < 8; ++i) {
        sim_time += kSimPerRTT;
        netudp_client_send(client, 0, payload, kPayload, NETUDP_SEND_UNRELIABLE);
        netudp_client_update(client, sim_time);
        netudp_server_update(server, sim_time);
        netudp_message_t* srv_msgs[16];
        int sn = netudp_server_receive(server, 0, srv_msgs, 16);
        for (int m = 0; m < sn; ++m) { netudp_message_release(srv_msgs[m]); }
        sim_time += kSimPerRTT;
        netudp_server_update(server, sim_time); /* flush echo */
        netudp_client_update(client, sim_time);
        netudp_message_t* cli_msgs[16];
        int cn = netudp_client_receive(client, cli_msgs, 16);
        for (int m = 0; m < cn; ++m) { netudp_message_release(cli_msgs[m]); }
    }

    /* Measure RTT for cfg.measure_iters pings */
    const int samples = (cfg.measure_iters > 1) ? cfg.measure_iters : 100;
    r.samples_ns.reserve(static_cast<size_t>(samples));

    for (int i = 0; i < samples; ++i) {
        auto t0 = Clock::now();

        /* 1. Client sends ping (sim_time += kSimPerRTT covers rate limiter) */
        sim_time += kSimPerRTT;
        netudp_client_send(client, 0, payload, kPayload, NETUDP_SEND_UNRELIABLE);
        netudp_client_update(client, sim_time);

        /* 2. Server receives + echo handler queues response */
        netudp_server_update(server, sim_time);
        netudp_message_t* srv_msgs[8];
        int sn = netudp_server_receive(server, 0, srv_msgs, 8);
        for (int m = 0; m < sn; ++m) { netudp_message_release(srv_msgs[m]); }

        /* 3. Server flushes echo; client receives (same simulated step) */
        sim_time += kSimPerRTT;
        netudp_server_update(server, sim_time);
        netudp_client_update(client, sim_time);
        netudp_message_t* cli_msgs[8];
        int cn = netudp_client_receive(client, cli_msgs, 8);
        for (int m = 0; m < cn; ++m) { netudp_message_release(cli_msgs[m]); }

        auto t1 = Clock::now();
        double rtt_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        r.samples_ns.push_back(rtt_ns);
    }

    /* ops_per_sec = pings per second based on p50 RTT */
    /* p50 is computed by finalize() in bench_main; estimate from samples mean */
    if (!r.samples_ns.empty()) {
        double sum = 0.0;
        for (double v : r.samples_ns) { sum += v; }
        double mean_ns = sum / static_cast<double>(r.samples_ns.size());
        r.ops_per_sec = (mean_ns > 0.0) ? (1e9 / mean_ns) : 0.0;
    }

    /* Cleanup */
    netudp_client_disconnect(client);
    netudp_client_destroy(client);
    netudp_server_stop(server);
    netudp_server_destroy(server);

    return r;
}

void register_latency_bench(BenchRegistry& reg) {
    reg.add("latency_rtt", [](const BenchConfig& cfg) -> BenchResult {
        return run_latency_bench(cfg);
    });
}
