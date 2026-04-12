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

/* --- Receive --- */

int  netudp_server_receive(netudp_server_t* server, int client_index,
                           netudp_message_t** messages, int max_messages);
int  netudp_client_receive(netudp_client_t* client,
                           netudp_message_t** messages, int max_messages);
void netudp_message_release(netudp_message_t* message);

/* --- Address --- */

int   netudp_parse_address(const char* str, netudp_address_t* addr);
char* netudp_address_to_string(const netudp_address_t* addr, char* buf, int buf_len);
int   netudp_address_equal(const netudp_address_t* a, const netudp_address_t* b);

#ifdef __cplusplus
}
#endif

#endif /* NETUDP_H */
