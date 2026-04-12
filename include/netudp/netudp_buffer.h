#ifndef NETUDP_BUFFER_H
#define NETUDP_BUFFER_H

/**
 * @file netudp_buffer.h
 * @brief Zero-copy buffer acquire/send and read/write helpers.
 */

#include "netudp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Buffer acquire/send (zero-copy) --- */

netudp_buffer_t* netudp_server_acquire_buffer(netudp_server_t* server);
int  netudp_server_send_buffer(netudp_server_t* server, int client_index,
                               int channel, netudp_buffer_t* buf, int flags);

/* --- Buffer write helpers --- */

void     netudp_buffer_write_u8(netudp_buffer_t* buf, uint8_t v);
void     netudp_buffer_write_u16(netudp_buffer_t* buf, uint16_t v);
void     netudp_buffer_write_u32(netudp_buffer_t* buf, uint32_t v);
void     netudp_buffer_write_u64(netudp_buffer_t* buf, uint64_t v);
void     netudp_buffer_write_f32(netudp_buffer_t* buf, float v);
void     netudp_buffer_write_f64(netudp_buffer_t* buf, double v);
void     netudp_buffer_write_varint(netudp_buffer_t* buf, int32_t v);
void     netudp_buffer_write_bytes(netudp_buffer_t* buf, const void* data, int len);
void     netudp_buffer_write_string(netudp_buffer_t* buf, const char* str, int max_len);

/* --- Buffer read helpers --- */

uint8_t  netudp_buffer_read_u8(netudp_buffer_t* buf);
uint16_t netudp_buffer_read_u16(netudp_buffer_t* buf);
uint32_t netudp_buffer_read_u32(netudp_buffer_t* buf);
uint64_t netudp_buffer_read_u64(netudp_buffer_t* buf);
float    netudp_buffer_read_f32(netudp_buffer_t* buf);
double   netudp_buffer_read_f64(netudp_buffer_t* buf);
int32_t  netudp_buffer_read_varint(netudp_buffer_t* buf);

#ifdef __cplusplus
}
#endif

#endif /* NETUDP_BUFFER_H */
