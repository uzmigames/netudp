/**
 * @file bench_throughput.cpp
 * @brief Real-world throughput benchmark: multiple clients, burst send, pipeline mode.
 *
 * Simulates a game server scenario:
 * - N clients connected simultaneously (4, 16, 64)
 * - Each client sends msgs_per_tick messages every tick (simulating position updates)
 * - Server receives all, counts total delivered msgs/s
 * - Tests single-thread vs pipeline (num_io_threads=2)
 * - Realistic tick rate (1ms ticks = 1000 Hz)
 */

#include "bench_main.h"
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

static constexpr uint64_t kThrProtoId  = 0xABCD000200000002ULL;
static constexpr int      kThrMsgSize  = 48;
static constexpr double   kThrBenchSec = 5.0;

static const uint8_t kThrKey[32] = {
    0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,
    0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8,
    0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8,
    0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8,
};

static uint8_t g_crypto_mode = 0; /* 0=AUTO (AES-GCM if AES-NI), 1=AES-GCM, 2=XChaCha20 */

static BenchResult run_throughput(const BenchConfig& cfg,
                                   int num_clients, int msgs_per_tick,
                                   int num_io_threads, int port_offset) {
    using Clock = std::chrono::high_resolution_clock;

    BenchResult r;
    char name[64];
    std::snprintf(name, sizeof(name), "thr_%dc_%dm_t%d",
                  num_clients, msgs_per_tick, num_io_threads);
    r.name = name;

    uint16_t port = static_cast<uint16_t>(30100 + port_offset);
    char srv_addr[64];
    std::snprintf(srv_addr, sizeof(srv_addr), "127.0.0.1:%u",
                  static_cast<unsigned>(port));

    /* Server config */
    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id = kThrProtoId;
    std::memcpy(srv_cfg.private_key, kThrKey, 32);
    srv_cfg.num_channels = 2;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
    srv_cfg.channels[1].type = NETUDP_CHANNEL_RELIABLE_ORDERED;
    srv_cfg.num_io_threads = num_io_threads;
    srv_cfg.crypto_mode = g_crypto_mode;

    double sim_time = 10000.0;
    netudp_server_t* server = netudp_server_create(srv_addr, &srv_cfg, sim_time);
    if (server == nullptr) {
        std::fprintf(stderr, "[thr] server_create failed\n");
        return r;
    }
    netudp_server_start(server, num_clients + 8);

    /* Connect N clients */
    std::vector<netudp_client_t*> clients(static_cast<size_t>(num_clients), nullptr);
    for (int i = 0; i < num_clients; ++i) {
        const char* addrs[] = { srv_addr };
        uint8_t token[2048] = {};
        netudp_generate_connect_token(1, addrs, 300, 10,
                                       static_cast<uint64_t>(70001 + i),
                                       kThrProtoId, kThrKey, nullptr, token);

        netudp_client_config_t cli_cfg = {};
        cli_cfg.protocol_id = kThrProtoId;
        cli_cfg.num_channels = 2;
        cli_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
        cli_cfg.channels[1].type = NETUDP_CHANNEL_RELIABLE_ORDERED;

        clients[static_cast<size_t>(i)] = netudp_client_create(nullptr, &cli_cfg, sim_time);
        if (clients[static_cast<size_t>(i)] != nullptr) {
            netudp_client_connect(clients[static_cast<size_t>(i)], token);
        }
    }

    /* Handshake all clients */
    auto deadline = Clock::now() + std::chrono::milliseconds(5000);
    int connected = 0;
    while (Clock::now() < deadline && connected < num_clients) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        connected = 0;
        for (int i = 0; i < num_clients; ++i) {
            if (clients[static_cast<size_t>(i)] != nullptr) {
                netudp_client_update(clients[static_cast<size_t>(i)], sim_time);
                if (netudp_client_state(clients[static_cast<size_t>(i)]) == 3) {
                    ++connected;
                }
            }
        }
        if (connected >= num_clients) { break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::printf("          %d/%d clients connected (threads=%d)\n",
                connected, num_clients, num_io_threads);

    if (connected < num_clients / 2) {
        std::fprintf(stderr, "[thr] handshake failed (%d/%d)\n", connected, num_clients);
        for (auto* c : clients) { if (c) { netudp_client_destroy(c); } }
        netudp_server_destroy(server);
        return r;
    }

    /* Prepare message payload */
    uint8_t msg[kThrMsgSize];
    std::memset(msg, 0xAA, kThrMsgSize);

    /* Measurement loop */
    for (int run = 0; run < cfg.measure_iters; ++run) {
        uint64_t total_sent = 0;
        uint64_t total_received = 0;

        auto t0 = Clock::now();
        auto end = t0 + std::chrono::milliseconds(static_cast<int>(kThrBenchSec * 1000));

        while (Clock::now() < end) {
            sim_time += 0.016; /* 60 Hz tick */

            /* Each client sends msgs_per_tick messages */
            for (int c = 0; c < num_clients; ++c) {
                auto* cli = clients[static_cast<size_t>(c)];
                if (cli == nullptr || netudp_client_state(cli) != 3) { continue; }
                for (int m = 0; m < msgs_per_tick; ++m) {
                    int rc = netudp_client_send(cli, 0, msg, kThrMsgSize,
                                                NETUDP_SEND_NO_NAGLE);
                    if (rc == NETUDP_OK) { ++total_sent; }
                }
                netudp_client_update(cli, sim_time);
            }

            /* Server processes */
            netudp_server_update(server, sim_time);

            /* Drain ALL receive queues */
            for (int c = 0; c < num_clients; ++c) {
                netudp_message_t* msgs_out[64];
                int n = netudp_server_receive(server, c, msgs_out, 64);
                while (n > 0) {
                    for (int j = 0; j < n; ++j) {
                        ++total_received;
                        netudp_message_release(msgs_out[j]);
                    }
                    n = netudp_server_receive(server, c, msgs_out, 64);
                }
            }

            /* No sleep — run at max throughput */
        }

        /* Drain remaining */
        for (int d = 0; d < 200; ++d) {
            sim_time += 0.016;
            for (auto* c : clients) {
                if (c && netudp_client_state(c) == 3) {
                    netudp_client_update(c, sim_time);
                }
            }
            netudp_server_update(server, sim_time);
            for (int c = 0; c < num_clients; ++c) {
                netudp_message_t* msgs_out[64];
                int n = netudp_server_receive(server, c, msgs_out, 64);
                for (int j = 0; j < n; ++j) {
                    ++total_received;
                    netudp_message_release(msgs_out[j]);
                }
            }
        }

        auto t1 = Clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        double msgs_per_sec = static_cast<double>(total_received) / elapsed;

        std::printf("          sent=%llu  recv=%llu  elapsed=%.2fs  msgs/s=%.0f  pps=%.0f\n",
                    static_cast<unsigned long long>(total_sent),
                    static_cast<unsigned long long>(total_received),
                    elapsed, msgs_per_sec,
                    msgs_per_sec); /* with coalescing, msgs/s > pps */

        r.samples_ns.push_back(elapsed * 1e9 / static_cast<double>(total_received > 0 ? total_received : 1));
        r.ops_per_sec = msgs_per_sec;
    }

    /* Cleanup */
    for (auto* c : clients) { if (c) { netudp_client_destroy(c); } }
    netudp_server_destroy(server);
    return r;
}

void register_throughput_bench(BenchRegistry& reg) {
    /* Small: 4 clients, 5 msgs/tick */
    reg.add("thr_4c_5m_t1", [](const BenchConfig& c) {
        return run_throughput(c, 4, 5, 1, 0);
    });
    /* Medium: 64 clients, 3 msgs/tick */
    reg.add("thr_64c_3m_t1", [](const BenchConfig& c) {
        return run_throughput(c, 64, 3, 1, 1);
    });
    /* Large: 256 clients, 3 msgs/tick */
    reg.add("thr_256c_3m_t1", [](const BenchConfig& c) {
        return run_throughput(c, 256, 3, 1, 2);
    });
    /* MMO: 1000 clients, 2 msgs/tick */
    reg.add("thr_1000c_2m_t1", [](const BenchConfig& c) {
        return run_throughput(c, 1000, 2, 1, 3);
    });
    /* MMO: 5000 clients, 2 msgs/tick, pipeline */
    reg.add("thr_5000c_2m_t2", [](const BenchConfig& c) {
        return run_throughput(c, 5000, 2, 2, 4);
    });
    /* MMO MAX: 5000 clients, 2 msgs/tick, pipeline + 4 workers */
    reg.add("thr_5000c_2m_t5", [](const BenchConfig& c) {
        return run_throughput(c, 5000, 2, 5, 5);
    });
}
