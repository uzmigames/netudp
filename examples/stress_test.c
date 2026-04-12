/**
 * @file stress_test.c
 * @brief Stress test — 1000 simulated client connections.
 *
 * Creates a server and spawns N clients (default 1000) in a single thread
 * using simulated time.  Each client sends 10 messages per simulated second.
 * Runs for 60 simulated seconds and reports:
 *   - Total messages sent / received
 *   - Packet loss percentage
 *   - Peak memory (RSS, via platform API)
 *   - Average PPS
 *
 * Usage:
 *   stress_test [num_clients] [duration_sec]
 *   stress_test 200 30
 */

#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
static uint64_t get_rss_bytes(void) {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return (uint64_t)pmc.WorkingSetSize;
    }
    return 0;
}
#elif defined(__APPLE__)
#include <mach/mach.h>
static uint64_t get_rss_bytes(void) {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) == KERN_SUCCESS) {
        return (uint64_t)info.resident_size;
    }
    return 0;
}
#else
#include <sys/resource.h>
static uint64_t get_rss_bytes(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return (uint64_t)ru.ru_maxrss * 1024;
}
#endif

static const uint8_t kPrivateKey[32] = {
    0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22, 0x33,
    0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB,
    0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22, 0x33,
    0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB,
};

static const uint64_t kProtocolId = 0x5773557355735573ULL;

#define MAX_CLIENTS 1024
#define PAYLOAD_SIZE 64
#define MSGS_PER_SEC 10

typedef struct {
    uint64_t received;
} StressCounter;

static void stress_handler(void* ctx, int ci, const void* data, int size, int ch) {
    (void)ci; (void)data; (void)size; (void)ch;
    ((StressCounter*)ctx)->received++;
}

int main(int argc, char** argv) {
    int num_clients  = (argc > 1) ? atoi(argv[1]) : 200;
    int duration_sec = (argc > 2) ? atoi(argv[2]) : 30;

    if (num_clients > MAX_CLIENTS) { num_clients = MAX_CLIENTS; }

    printf("[stress] %d clients  duration=%ds\n", num_clients, duration_sec);

    if (netudp_init() != NETUDP_OK) {
        fprintf(stderr, "netudp_init() failed\n");
        return 1;
    }

    const char* bind_addr = "127.0.0.1:7779";

    netudp_server_config_t srv_cfg;
    memset(&srv_cfg, 0, sizeof(srv_cfg));
    srv_cfg.protocol_id = kProtocolId;
    memcpy(srv_cfg.private_key, kPrivateKey, 32);
    srv_cfg.num_channels = 1;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;

    StressCounter counter;
    counter.received = 0;

    double sim_time = (double)time(NULL);
    netudp_server_t* server = netudp_server_create(bind_addr, &srv_cfg, sim_time);
    if (server == NULL) {
        fprintf(stderr, "Failed to create server\n");
        netudp_term();
        return 1;
    }
    netudp_server_start(server, num_clients + 8);
    netudp_server_set_packet_handler(server, 0x01, stress_handler, &counter);

    /* Allocate client array */
    netudp_client_t** clients = (netudp_client_t**)calloc(
        (size_t)num_clients, sizeof(netudp_client_t*));
    if (clients == NULL) {
        fprintf(stderr, "OOM\n");
        netudp_server_destroy(server);
        netudp_term();
        return 1;
    }

    const char* addrs[] = { bind_addr };

    /* Connect all clients */
    printf("[stress] connecting %d clients...\n", num_clients);
    for (int i = 0; i < num_clients; ++i) {
        uint8_t token[2048];
        if (netudp_generate_connect_token(1, addrs, 300, 10,
                                          (uint64_t)(30001 + i), kProtocolId,
                                          kPrivateKey, NULL, token) != NETUDP_OK) {
            continue;
        }
        netudp_client_config_t cli_cfg;
        memset(&cli_cfg, 0, sizeof(cli_cfg));
        cli_cfg.protocol_id  = kProtocolId;
        cli_cfg.num_channels = 1;
        cli_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
        clients[i] = netudp_client_create(NULL, &cli_cfg, sim_time);
        if (clients[i] != NULL) {
            netudp_client_connect(clients[i], token);
        }
    }

    /* Handshake phase — 5 simulated seconds */
    for (int tick = 0; tick < 312; ++tick) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);
        for (int i = 0; i < num_clients; ++i) {
            if (clients[i] != NULL) { netudp_client_update(clients[i], sim_time); }
        }
        netudp_message_t* msgs[256];
        int n = netudp_server_receive_batch(server, msgs, 256);
        for (int m = 0; m < n; ++m) { netudp_message_release(msgs[m]); }
    }

    int connected = 0;
    for (int i = 0; i < num_clients; ++i) {
        if (clients[i] != NULL && netudp_client_state(clients[i]) == 3) { connected++; }
    }
    printf("[stress] %d/%d clients connected\n", connected, num_clients);

    uint64_t rss_before = get_rss_bytes();

    /* Measurement phase */
    uint64_t total_sent = 0;
    int total_ticks = (int)(duration_sec / 0.016);
    /* send interval: every (1/MSGS_PER_SEC) / 0.016 ticks */
    int send_every = (int)(1.0 / (MSGS_PER_SEC * 0.016));
    if (send_every < 1) { send_every = 1; }

    uint8_t payload[PAYLOAD_SIZE];
    memset(payload, 0, sizeof(payload));
    payload[0] = 0x01;

    double phase_start = sim_time;

    for (int tick = 0; tick < total_ticks; ++tick) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);

        if (tick % send_every == 0) {
            for (int i = 0; i < num_clients; ++i) {
                if (clients[i] != NULL && netudp_client_state(clients[i]) == 3) {
                    netudp_client_send(clients[i], 0, payload, PAYLOAD_SIZE,
                                       NETUDP_SEND_UNRELIABLE);
                    total_sent++;
                }
            }
        }

        for (int i = 0; i < num_clients; ++i) {
            if (clients[i] != NULL) { netudp_client_update(clients[i], sim_time); }
        }

        netudp_message_t* msgs[512];
        int n = netudp_server_receive_batch(server, msgs, 512);
        for (int m = 0; m < n; ++m) { netudp_message_release(msgs[m]); }
    }

    double elapsed = sim_time - phase_start;
    uint64_t rss_after  = get_rss_bytes();

    /* Report */
    uint64_t recv = counter.received;
    double loss   = (total_sent > 0)
                  ? 100.0 * (1.0 - (double)recv / (double)total_sent)
                  : 0.0;
    double pps    = (elapsed > 0.0) ? (double)recv / elapsed : 0.0;
    double mem_mb = (double)(rss_after - rss_before) / (1024.0 * 1024.0);

    printf("\n[results]\n");
    printf("  clients connected : %d / %d\n",  connected, num_clients);
    printf("  total sent        : %llu\n",      (unsigned long long)total_sent);
    printf("  total received    : %llu\n",      (unsigned long long)recv);
    printf("  packet loss       : %.2f%%\n",    loss);
    printf("  avg pps           : %.0f\n",      pps);
    printf("  memory delta      : %.1f MiB\n",  mem_mb);

    /* Cleanup */
    for (int i = 0; i < num_clients; ++i) {
        if (clients[i] != NULL) {
            netudp_client_disconnect(clients[i]);
            netudp_client_destroy(clients[i]);
        }
    }
    free(clients);
    netudp_server_stop(server);
    netudp_server_destroy(server);
    netudp_term();
    return 0;
}
