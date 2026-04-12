/**
 * @file echo_client.c
 * @brief Echo client — connects to echo_server, sends messages, verifies RTT.
 *
 * Usage:
 *   echo_client [server_address] [num_messages]
 *   echo_client 127.0.0.1:7777 100
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
static uint64_t get_time_us(void) {
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (uint64_t)(cnt.QuadPart * 1000000 / freq.QuadPart);
}
#else
#include <unistd.h>
#include <sys/time.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
static uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}
#endif

static const uint8_t kPrivateKey[32] = {
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
};

static const uint64_t kProtocolId = 0xDEADBEEF12345678ULL;

/* Payload: [seq_hi][seq_lo][send_time_us 8 bytes][padding...] */
#define PAYLOAD_SIZE 64

static void build_payload(uint8_t* buf, uint16_t seq, uint64_t send_us) {
    memset(buf, 0, PAYLOAD_SIZE);
    buf[0] = (uint8_t)(seq >> 8);
    buf[1] = (uint8_t)(seq & 0xFF);
    memcpy(buf + 2, &send_us, 8);
}

int main(int argc, char** argv) {
    const char* srv_addr   = (argc > 1) ? argv[1] : "127.0.0.1:7777";
    int         num_msgs   = (argc > 2) ? atoi(argv[2]) : 20;

    if (netudp_init() != NETUDP_OK) {
        fprintf(stderr, "netudp_init() failed\n");
        return 1;
    }

    /* Generate connect token */
    const char* addrs[] = { srv_addr };
    uint8_t token[2048];
    if (netudp_generate_connect_token(1, addrs, 300, 10,
                                      42ULL, kProtocolId,
                                      kPrivateKey, NULL, token) != NETUDP_OK) {
        fprintf(stderr, "Failed to generate connect token\n");
        netudp_term();
        return 1;
    }

    netudp_client_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.protocol_id  = kProtocolId;
    cfg.num_channels = 2;
    cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
    cfg.channels[1].type = NETUDP_CHANNEL_RELIABLE_ORDERED;

    double sim_time = (double)time(NULL);
    netudp_client_t* client = netudp_client_create(NULL, &cfg, sim_time);
    if (client == NULL) {
        fprintf(stderr, "Failed to create client\n");
        netudp_term();
        return 1;
    }
    netudp_client_connect(client, token);

    /* Wait for connection */
    printf("[client] connecting to %s...\n", srv_addr);
    int attempts = 0;
    while (netudp_client_state(client) != 3 && attempts < 300) {
        sim_time += 0.016;
        netudp_client_update(client, sim_time);
        SLEEP_MS(16);
        ++attempts;
    }

    if (netudp_client_state(client) != 3) {
        fprintf(stderr, "[client] connection failed (state=%d)\n",
                netudp_client_state(client));
        netudp_client_destroy(client);
        netudp_term();
        return 1;
    }
    printf("[client] connected!\n");

    /* Send messages and measure RTT */
    uint64_t total_rtt_us = 0;
    int      received     = 0;
    uint8_t  payload[PAYLOAD_SIZE];

    for (int i = 0; i < num_msgs; ++i) {
        uint64_t send_us = get_time_us();
        build_payload(payload, (uint16_t)i, send_us);

        netudp_client_send(client, 0, payload, PAYLOAD_SIZE, NETUDP_SEND_UNRELIABLE);

        /* Wait up to 200ms for echo */
        int wait_ms = 0;
        while (wait_ms < 200) {
            sim_time += 0.016;
            netudp_client_update(client, sim_time);

            netudp_message_t* msgs[8];
            int n = netudp_client_receive(client, msgs, 8);
            for (int m = 0; m < n; ++m) {
                if (msgs[m]->size >= PAYLOAD_SIZE) {
                    uint64_t sent_us = 0;
                    memcpy(&sent_us, (const uint8_t*)msgs[m]->data + 2, 8);
                    uint64_t rtt = get_time_us() - sent_us;
                    total_rtt_us += rtt;
                    received++;
                    printf("[rtt] seq=%d  rtt=%llu us\n",
                           i, (unsigned long long)rtt);
                }
                netudp_message_release(msgs[m]);
            }
            if (received == i + 1) { break; }
            SLEEP_MS(16);
            wait_ms += 16;
        }

        SLEEP_MS(50);
    }

    printf("\n[summary] sent=%d  received=%d  avg_rtt=%llu us\n",
           num_msgs, received,
           received > 0 ? (unsigned long long)(total_rtt_us / (uint64_t)received) : 0ULL);

    netudp_client_disconnect(client);
    netudp_client_destroy(client);
    netudp_term();
    return 0;
}
