/**
 * @file echo_server.c
 * @brief Minimal echo server — receives messages and echoes them back.
 *
 * Usage:
 *   echo_server [bind_address]
 *   echo_server 0.0.0.0:7777
 *
 * Generate a connect token with the matching private key and use
 * echo_client to connect.
 */

#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

/* ---------------------------------------------------------------------- */

static const uint8_t kPrivateKey[32] = {
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
};

static const uint64_t kProtocolId = 0xDEADBEEF12345678ULL;

typedef struct {
    netudp_server_t* server;
    uint64_t         total_messages;
} EchoState;

static void on_connect(void* ctx, int ci, uint64_t client_id,
                       const uint8_t user_data[256]) {
    (void)user_data;
    printf("[server] client %d connected  (client_id=%llu)\n",
           ci, (unsigned long long)client_id);
}

static void on_disconnect(void* ctx, int ci, int reason) {
    (void)ctx;
    printf("[server] client %d disconnected (reason=%d)\n", ci, reason);
}

static void echo_handler(void* ctx, int ci, const void* data, int size, int channel) {
    EchoState* s = (EchoState*)ctx;
    s->total_messages++;
    /* Echo back on same channel */
    netudp_server_send(s->server, ci, channel, data, size, NETUDP_SEND_UNRELIABLE);
}

int main(int argc, char** argv) {
    const char* bind_addr = (argc > 1) ? argv[1] : "0.0.0.0:7777";

    if (netudp_init() != NETUDP_OK) {
        fprintf(stderr, "netudp_init() failed\n");
        return 1;
    }

    netudp_server_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.protocol_id   = kProtocolId;
    memcpy(cfg.private_key, kPrivateKey, 32);
    cfg.num_channels  = 2;
    cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
    cfg.channels[1].type = NETUDP_CHANNEL_RELIABLE_ORDERED;

    EchoState state = { NULL, 0 };
    cfg.callback_context = &state;
    cfg.on_connect       = on_connect;
    cfg.on_disconnect    = on_disconnect;

    double sim_time = (double)time(NULL);
    netudp_server_t* server = netudp_server_create(bind_addr, &cfg, sim_time);
    if (server == NULL) {
        fprintf(stderr, "Failed to create server on %s\n", bind_addr);
        netudp_term();
        return 1;
    }
    state.server = server;
    netudp_server_start(server, 64);

    /* Register echo handler for all packet types 0-255 */
    for (int t = 0; t < 256; ++t) {
        netudp_server_set_packet_handler(server, (uint16_t)t, echo_handler, &state);
    }

    printf("[server] listening on %s  (protocol_id=0x%016llX)\n",
           bind_addr, (unsigned long long)kProtocolId);

    double last_stats = sim_time;

    for (;;) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);

        /* Drain receive queue */
        netudp_message_t* msgs[64];
        int max_clients = netudp_server_max_clients(server);
        for (int ci = 0; ci < max_clients; ++ci) {
            int n = netudp_server_receive(server, ci, msgs, 64);
            for (int m = 0; m < n; ++m) {
                netudp_message_release(msgs[m]);
            }
        }

        /* Print stats every 5 seconds */
        if (sim_time - last_stats >= 5.0) {
            last_stats = sim_time;
            netudp_server_stats_t stats;
            netudp_server_get_stats(server, &stats);
            printf("[stats] clients=%d  total_echoed=%llu  pps=%.0f\n",
                   stats.connected_clients,
                   (unsigned long long)state.total_messages,
                   stats.recv_pps);
        }

        SLEEP_MS(16);
    }

    netudp_server_stop(server);
    netudp_server_destroy(server);
    netudp_term();
    return 0;
}
