#ifndef NETUDP_H
#define NETUDP_H

/**
 * @file netudp.h
 * @brief Main public API for netudp — high-performance UDP networking.
 *
 * All functions use C linkage for universal FFI compatibility.
 * All types are POD structs defined in netudp_types.h.
 */

#include "netudp_config.h"
#include "netudp_types.h"
#include "netudp_buffer.h"
#include "netudp_token.h"
#include "netudp_profiling.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Lifecycle --- */

int  netudp_init(void);
void netudp_term(void);

/* --- SIMD query --- */

/* Returns the detected SIMD level (netudp_simd_level_t) or -1 if called before init. */
int netudp_simd_level(void);

/* --- Server --- */

netudp_server_t* netudp_server_create(const char* address,
    const netudp_server_config_t* config, double time);
void netudp_server_start(netudp_server_t* server, int max_clients);
int  netudp_server_max_clients(const netudp_server_t* server);
void netudp_server_stop(netudp_server_t* server);
void netudp_server_update(netudp_server_t* server, double time);
void netudp_server_destroy(netudp_server_t* server);

/* --- Client --- */

netudp_client_t* netudp_client_create(const char* address,
    const netudp_client_config_t* config, double time);
void netudp_client_connect(netudp_client_t* client, uint8_t connect_token[2048]);
void netudp_client_update(netudp_client_t* client, double time);
void netudp_client_disconnect(netudp_client_t* client);
void netudp_client_destroy(netudp_client_t* client);
int  netudp_client_state(const netudp_client_t* client);

/* --- Send --- */

int  netudp_server_send(netudp_server_t* server, int client_index,
                        int channel, const void* data, int bytes, int flags);
int  netudp_client_send(netudp_client_t* client,
                        int channel, const void* data, int bytes, int flags);
void netudp_server_broadcast(netudp_server_t* server, int channel,
                             const void* data, int bytes, int flags);
void netudp_server_broadcast_except(netudp_server_t* server, int except_client,
                                    int channel, const void* data, int bytes, int flags);
void netudp_server_flush(netudp_server_t* server, int client_index);
void netudp_client_flush(netudp_client_t* client);

/* --- Packet handler dispatch --- */

/* Register a callback for messages whose first byte equals packet_type.
 * Matching messages are delivered to fn instead of the normal receive queue.
 * Pass fn=NULL to unregister.  Max 256 handlers (packet_type 0-255). */
void netudp_server_set_packet_handler(netudp_server_t* server, uint16_t packet_type,
                                      netudp_packet_handler_fn fn, void* ctx);

/* --- Batch send --- */

/* Descriptor for one entry in a batch send call. */
typedef struct netudp_send_entry {
    int         client_index;
    int         channel;
    const void* data;
    int         bytes;
    int         flags;
} netudp_send_entry_t;

/* Queue up to count messages in one call.  Returns number queued. */
int netudp_server_send_batch(netudp_server_t* server,
                             const netudp_send_entry_t* entries, int count);

/* --- Receive --- */

int  netudp_server_receive(netudp_server_t* server, int client_index,
                           netudp_message_t** messages, int max_messages);
int  netudp_client_receive(netudp_client_t* client,
                           netudp_message_t** messages, int max_messages);
void netudp_message_release(netudp_message_t* message);

/* Receive from all clients in one call.  Fills out[0..n-1] with message
 * pointers across all connection slots.  Caller releases each message.
 * Returns total messages received. */
int netudp_server_receive_batch(netudp_server_t* server,
                                netudp_message_t** out, int max_messages);

/* --- Threading --- */

/** Returns the number of active I/O threads (1 = single-threaded). */
int  netudp_server_num_io_threads(const netudp_server_t* server);

/**
 * Pin an I/O thread to a specific CPU core (Linux only).
 * @param thread_index  0-based I/O thread index.
 * @param cpu_id        CPU core to pin to, or -1 to unpin.
 * @return NETUDP_OK on success, error code on failure.
 */
int  netudp_server_set_thread_affinity(netudp_server_t* server,
                                       int thread_index, int cpu_id);

/* --- Windows diagnostics --- */

/**
 * Check if Windows Filtering Platform (WFP) is active.
 * WFP adds ~2us/packet overhead. Returns 1 if active, 0 if not, -1 on error.
 * Always returns 0 on non-Windows platforms.
 */
int  netudp_windows_is_wfp_active(void);

/* --- Stats --- */

typedef struct netudp_server_stats {
    int      connected_clients;
    int      max_clients;
    double   recv_pps;   /* incoming packets per second */
    double   send_pps;   /* outgoing packets per second */
    uint64_t total_bytes_recv;
    uint64_t total_bytes_sent;
    uint8_t  ddos_severity;
} netudp_server_stats_t;

void netudp_server_get_stats(const netudp_server_t* server,
                             netudp_server_stats_t* out);

/* --- Address --- */

int   netudp_parse_address(const char* str, netudp_address_t* addr);
char* netudp_address_to_string(const netudp_address_t* addr, char* buf, int buf_len);
int   netudp_address_equal(const netudp_address_t* a, const netudp_address_t* b);

#ifdef __cplusplus
}
#endif

#endif /* NETUDP_H */
