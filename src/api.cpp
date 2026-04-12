#include <netudp/netudp.h>
#include "core/platform.h"
#include "simd/netudp_simd.h"

#include <atomic>

static std::atomic<bool> g_initialized{false};
static netudp_simd_level_t g_detected_simd_level = NETUDP_SIMD_GENERIC;

extern "C" {

int netudp_init(void) {
    if (g_initialized.exchange(true)) {
        return NETUDP_OK;
    }
    g_detected_simd_level = netudp::simd::detect_and_set();
    return NETUDP_OK;
}

void netudp_term(void) {
    if (!g_initialized.exchange(false)) {
        return;
    }
    netudp::simd::g_simd = nullptr;
    g_detected_simd_level = NETUDP_SIMD_GENERIC;
}

int netudp_simd_level(void) {
    if (!g_initialized.load()) {
        return -1;
    }
    return g_detected_simd_level;
}

/* --- Server lifecycle (Phase 2 implementation) --- */

netudp_server_t* netudp_server_create(const char* /*address*/,
    const netudp_server_config_t* /*config*/, double /*time*/) {
    return nullptr;
}

void netudp_server_start(netudp_server_t* /*server*/, int /*max_clients*/) {}
void netudp_server_stop(netudp_server_t* /*server*/) {}
void netudp_server_update(netudp_server_t* /*server*/, double /*time*/) {}
void netudp_server_destroy(netudp_server_t* /*server*/) {}

/* --- Client lifecycle (Phase 2 implementation) --- */

netudp_client_t* netudp_client_create(const char* /*address*/,
    const netudp_client_config_t* /*config*/, double /*time*/) {
    return nullptr;
}

void netudp_client_connect(netudp_client_t* /*client*/, uint8_t /*connect_token*/[2048]) {}
void netudp_client_update(netudp_client_t* /*client*/, double /*time*/) {}
void netudp_client_disconnect(netudp_client_t* /*client*/) {}
void netudp_client_destroy(netudp_client_t* /*client*/) {}

int netudp_client_state(const netudp_client_t* /*client*/) {
    return 0;
}

/* --- Send (Phase 3 implementation) --- */

int netudp_server_send(netudp_server_t* /*server*/, int /*client_index*/,
                       int /*channel*/, const void* /*data*/, int /*bytes*/, int /*flags*/) {
    return NETUDP_ERROR_NOT_INITIALIZED;
}

int netudp_client_send(netudp_client_t* /*client*/,
                       int /*channel*/, const void* /*data*/, int /*bytes*/, int /*flags*/) {
    return NETUDP_ERROR_NOT_INITIALIZED;
}

void netudp_server_broadcast(netudp_server_t* /*server*/, int /*channel*/,
                             const void* /*data*/, int /*bytes*/, int /*flags*/) {}
void netudp_server_broadcast_except(netudp_server_t* /*server*/, int /*except_client*/,
                                    int /*channel*/, const void* /*data*/, int /*bytes*/,
                                    int /*flags*/) {}
void netudp_server_flush(netudp_server_t* /*server*/, int /*client_index*/) {}
void netudp_client_flush(netudp_client_t* /*client*/) {}

/* --- Receive (Phase 3 implementation) --- */

int netudp_server_receive(netudp_server_t* /*server*/, int /*client_index*/,
                          netudp_message_t** /*messages*/, int /*max_messages*/) {
    return 0;
}

int netudp_client_receive(netudp_client_t* /*client*/,
                          netudp_message_t** /*messages*/, int /*max_messages*/) {
    return 0;
}

void netudp_message_release(netudp_message_t* /*message*/) {}

/* Address functions implemented in src/core/address.cpp */

/* --- Buffer API (Phase 6 implementation) --- */

netudp_buffer_t* netudp_server_acquire_buffer(netudp_server_t* /*server*/) { return nullptr; }
int netudp_server_send_buffer(netudp_server_t* /*server*/, int /*client_index*/,
                              int /*channel*/, netudp_buffer_t* /*buf*/, int /*flags*/) {
    return NETUDP_ERROR_NOT_INITIALIZED;
}

void     netudp_buffer_write_u8(netudp_buffer_t* /*buf*/, uint8_t /*v*/) {}
void     netudp_buffer_write_u16(netudp_buffer_t* /*buf*/, uint16_t /*v*/) {}
void     netudp_buffer_write_u32(netudp_buffer_t* /*buf*/, uint32_t /*v*/) {}
void     netudp_buffer_write_u64(netudp_buffer_t* /*buf*/, uint64_t /*v*/) {}
void     netudp_buffer_write_f32(netudp_buffer_t* /*buf*/, float /*v*/) {}
void     netudp_buffer_write_f64(netudp_buffer_t* /*buf*/, double /*v*/) {}
void     netudp_buffer_write_varint(netudp_buffer_t* /*buf*/, int32_t /*v*/) {}
void     netudp_buffer_write_bytes(netudp_buffer_t* /*buf*/, const void* /*data*/, int /*len*/) {}
void     netudp_buffer_write_string(netudp_buffer_t* /*buf*/, const char* /*str*/,
                                    int /*max_len*/) {}

uint8_t  netudp_buffer_read_u8(netudp_buffer_t* /*buf*/) { return 0; }
uint16_t netudp_buffer_read_u16(netudp_buffer_t* /*buf*/) { return 0; }
uint32_t netudp_buffer_read_u32(netudp_buffer_t* /*buf*/) { return 0; }
uint64_t netudp_buffer_read_u64(netudp_buffer_t* /*buf*/) { return 0; }
float    netudp_buffer_read_f32(netudp_buffer_t* /*buf*/) { return 0.0F; }
double   netudp_buffer_read_f64(netudp_buffer_t* /*buf*/) { return 0.0; }
int32_t  netudp_buffer_read_varint(netudp_buffer_t* /*buf*/) { return 0; }

/* --- Token generation (Phase 2 implementation) --- */

int netudp_generate_connect_token(
    int /*num_server_addresses*/, const char** /*server_addresses*/,
    int /*expire_seconds*/, int /*timeout_seconds*/,
    uint64_t /*client_id*/, uint64_t /*protocol_id*/,
    const uint8_t /*private_key*/[32], uint8_t /*user_data*/[256],
    uint8_t /*connect_token*/[2048]) {
    return NETUDP_ERROR_NOT_INITIALIZED;
}

} /* extern "C" */
