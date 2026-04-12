/**
 * @file chat_server.c
 * @brief Multi-client chat server.
 *
 * - Reliable ordered channel 0: chat messages (broadcast to all others)
 * - Unreliable channel 1:       typing indicators (broadcast to all others)
 *
 * Usage:
 *   chat_server [bind_address]
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

static const uint8_t kPrivateKey[32] = {
    0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22,
    0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA,
    0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22,
    0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA,
};

static const uint64_t kProtocolId = 0xCA7BEEF111111111ULL;

typedef struct {
    netudp_server_t* server;
    int              max_clients;
    uint64_t         total_messages;
    uint64_t         total_indicators;
} ChatState;

static void on_connect(void* ctx, int ci, uint64_t client_id,
                       const uint8_t user_data[256]) {
    (void)user_data;
    printf("[chat] client %d joined (id=%llu)\n",
           ci, (unsigned long long)client_id);
}

static void on_disconnect(void* ctx, int ci, int reason) {
    (void)ctx;
    printf("[chat] client %d left (reason=%d)\n", ci, reason);
}

/* Channel 0: reliable chat message — broadcast to all except sender */
static void chat_handler(void* ctx, int ci, const void* data, int size, int channel) {
    ChatState* s = (ChatState*)ctx;
    s->total_messages++;
    netudp_server_broadcast_except(s->server, ci, channel,
                                   data, size, NETUDP_SEND_RELIABLE);
}

/* Channel 1: typing indicator — broadcast to all except sender */
static void typing_handler(void* ctx, int ci, const void* data, int size, int channel) {
    ChatState* s = (ChatState*)ctx;
    s->total_indicators++;
    netudp_server_broadcast_except(s->server, ci, channel,
                                   data, size, NETUDP_SEND_UNRELIABLE);
}

int main(int argc, char** argv) {
    const char* bind_addr = (argc > 1) ? argv[1] : "0.0.0.0:7778";

    if (netudp_init() != NETUDP_OK) {
        fprintf(stderr, "netudp_init() failed\n");
        return 1;
    }

    netudp_server_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.protocol_id   = kProtocolId;
    memcpy(cfg.private_key, kPrivateKey, 32);
    cfg.num_channels  = 2;
    cfg.channels[0].type = NETUDP_CHANNEL_RELIABLE_ORDERED; /* chat */
    cfg.channels[1].type = NETUDP_CHANNEL_UNRELIABLE;       /* typing */

    ChatState state = { NULL, 0, 0, 0 };
    cfg.callback_context = &state;
    cfg.on_connect       = on_connect;
    cfg.on_disconnect    = on_disconnect;

    double sim_time = (double)time(NULL);
    netudp_server_t* server = netudp_server_create(bind_addr, &cfg, sim_time);
    if (server == NULL) {
        fprintf(stderr, "Failed to create chat server on %s\n", bind_addr);
        netudp_term();
        return 1;
    }
    state.server      = server;
    state.max_clients = 32;
    netudp_server_start(server, state.max_clients);

    /* Register per-packet-type handlers (type byte 0x01 = chat, 0x02 = typing) */
    netudp_server_set_packet_handler(server, 0x01, chat_handler,   &state);
    netudp_server_set_packet_handler(server, 0x02, typing_handler, &state);

    printf("[chat] server listening on %s\n", bind_addr);

    double last_stats = sim_time;

    for (;;) {
        sim_time += 0.016;
        netudp_server_update(server, sim_time);

        /* Drain receive queue — handlers above do the actual work */
        netudp_message_t* msgs[256];
        int n = netudp_server_receive_batch(server, msgs, 256);
        for (int m = 0; m < n; ++m) {
            netudp_message_release(msgs[m]);
        }

        if (sim_time - last_stats >= 5.0) {
            last_stats = sim_time;
            netudp_server_stats_t stats;
            netudp_server_get_stats(server, &stats);
            printf("[stats] clients=%d  messages=%llu  indicators=%llu\n",
                   stats.connected_clients,
                   (unsigned long long)state.total_messages,
                   (unsigned long long)state.total_indicators);
        }

        SLEEP_MS(16);
    }

    netudp_server_stop(server);
    netudp_server_destroy(server);
    netudp_term();
    return 0;
}
