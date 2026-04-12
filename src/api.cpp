#include <netudp/netudp.h>
#include <netudp/netudp_token.h>
#include <netudp/netudp_buffer.h>
#include "core/platform.h"
#include "core/address.h"
#include "simd/netudp_simd.h"
#include "socket/socket.h"
#include "connection/connect_token.h"
#include "crypto/xchacha.h"
#include "crypto/random.h"
#include "crypto/vendor/monocypher.h"

#include <atomic>
#include <cstring>
#include <ctime>

static std::atomic<bool> g_initialized{false};
static netudp_simd_level_t g_detected_simd_level = NETUDP_SIMD_GENERIC;

extern "C" {

int netudp_init(void) {
    if (g_initialized.exchange(true)) {
        return NETUDP_OK;
    }
    g_detected_simd_level = netudp::simd::detect_and_set();
    int sock_err = netudp::socket_platform_init();
    if (sock_err != NETUDP_OK) {
        g_initialized.store(false);
        return sock_err;
    }
    return NETUDP_OK;
}

void netudp_term(void) {
    if (!g_initialized.exchange(false)) {
        return;
    }
    netudp::socket_platform_term();
    netudp::simd::g_simd = nullptr;
    g_detected_simd_level = NETUDP_SIMD_GENERIC;
}

int netudp_simd_level(void) {
    if (!g_initialized.load()) {
        return -1;
    }
    return g_detected_simd_level;
}

/* Server/Client implemented in src/server.cpp and src/client.cpp */
/* Address implemented in src/core/address.cpp */
/* netudp_message_release implemented in src/server.cpp */

/* ======================================================================
 * Buffer API — real implementation
 * ====================================================================== */

struct netudp_buffer {
    uint8_t data[NETUDP_MTU];
    int     capacity;
    int     position;
};

static netudp_buffer g_buffer_pool[64] = {};
static bool g_buffer_in_use[64] = {};

netudp_buffer_t* netudp_server_acquire_buffer(netudp_server_t* /*server*/) {
    for (int i = 0; i < 64; ++i) {
        if (!g_buffer_in_use[i]) {
            g_buffer_in_use[i] = true;
            g_buffer_pool[i].capacity = NETUDP_MTU;
            g_buffer_pool[i].position = 0;
            std::memset(g_buffer_pool[i].data, 0, NETUDP_MTU);
            return &g_buffer_pool[i];
        }
    }
    return nullptr;
}

int netudp_server_send_buffer(netudp_server_t* server, int client_index,
                              int channel, netudp_buffer_t* buf, int flags) {
    if (server == nullptr || buf == nullptr) {
        return NETUDP_ERROR_INVALID_PARAM;
    }
    int result = netudp_server_send(server, client_index, channel,
                                     buf->data, buf->position, flags);
    /* Release buffer */
    for (int i = 0; i < 64; ++i) {
        if (&g_buffer_pool[i] == buf) {
            g_buffer_in_use[i] = false;
            break;
        }
    }
    return result;
}

#define BUF_CHECK(buf, needed) \
    if ((buf) == nullptr || (buf)->position + (needed) > (buf)->capacity) return

#define BUF_CHECK_R(buf, needed, ret) \
    if ((buf) == nullptr || (buf)->position + (needed) > (buf)->capacity) return (ret)

void netudp_buffer_write_u8(netudp_buffer_t* buf, uint8_t v) {
    BUF_CHECK(buf, 1);
    buf->data[buf->position++] = v;
}

void netudp_buffer_write_u16(netudp_buffer_t* buf, uint16_t v) {
    BUF_CHECK(buf, 2);
    std::memcpy(buf->data + buf->position, &v, 2);
    buf->position += 2;
}

void netudp_buffer_write_u32(netudp_buffer_t* buf, uint32_t v) {
    BUF_CHECK(buf, 4);
    std::memcpy(buf->data + buf->position, &v, 4);
    buf->position += 4;
}

void netudp_buffer_write_u64(netudp_buffer_t* buf, uint64_t v) {
    BUF_CHECK(buf, 8);
    std::memcpy(buf->data + buf->position, &v, 8);
    buf->position += 8;
}

void netudp_buffer_write_f32(netudp_buffer_t* buf, float v) {
    BUF_CHECK(buf, 4);
    std::memcpy(buf->data + buf->position, &v, 4);
    buf->position += 4;
}

void netudp_buffer_write_f64(netudp_buffer_t* buf, double v) {
    BUF_CHECK(buf, 8);
    std::memcpy(buf->data + buf->position, &v, 8);
    buf->position += 8;
}

void netudp_buffer_write_varint(netudp_buffer_t* buf, int32_t v) {
    BUF_CHECK(buf, 5); /* Max 5 bytes for 32-bit varint */
    uint32_t uv = static_cast<uint32_t>(v);
    while (uv > 0x7F) {
        buf->data[buf->position++] = static_cast<uint8_t>(uv & 0x7F) | 0x80;
        uv >>= 7;
    }
    buf->data[buf->position++] = static_cast<uint8_t>(uv);
}

void netudp_buffer_write_bytes(netudp_buffer_t* buf, const void* data, int len) {
    BUF_CHECK(buf, len);
    if (data != nullptr && len > 0) {
        std::memcpy(buf->data + buf->position, data, static_cast<size_t>(len));
        buf->position += len;
    }
}

void netudp_buffer_write_string(netudp_buffer_t* buf, const char* str, int max_len) {
    if (buf == nullptr || str == nullptr) {
        return;
    }
    int len = 0;
    while (len < max_len && str[len] != '\0') {
        len++;
    }
    BUF_CHECK(buf, 2 + len);
    uint16_t len16 = static_cast<uint16_t>(len);
    std::memcpy(buf->data + buf->position, &len16, 2);
    buf->position += 2;
    std::memcpy(buf->data + buf->position, str, static_cast<size_t>(len));
    buf->position += len;
}

uint8_t netudp_buffer_read_u8(netudp_buffer_t* buf) {
    BUF_CHECK_R(buf, 1, 0);
    return buf->data[buf->position++];
}

uint16_t netudp_buffer_read_u16(netudp_buffer_t* buf) {
    BUF_CHECK_R(buf, 2, 0);
    uint16_t v = 0;
    std::memcpy(&v, buf->data + buf->position, 2);
    buf->position += 2;
    return v;
}

uint32_t netudp_buffer_read_u32(netudp_buffer_t* buf) {
    BUF_CHECK_R(buf, 4, 0);
    uint32_t v = 0;
    std::memcpy(&v, buf->data + buf->position, 4);
    buf->position += 4;
    return v;
}

uint64_t netudp_buffer_read_u64(netudp_buffer_t* buf) {
    BUF_CHECK_R(buf, 8, 0);
    uint64_t v = 0;
    std::memcpy(&v, buf->data + buf->position, 8);
    buf->position += 8;
    return v;
}

float netudp_buffer_read_f32(netudp_buffer_t* buf) {
    BUF_CHECK_R(buf, 4, 0.0F);
    float v = 0.0F;
    std::memcpy(&v, buf->data + buf->position, 4);
    buf->position += 4;
    return v;
}

double netudp_buffer_read_f64(netudp_buffer_t* buf) {
    BUF_CHECK_R(buf, 8, 0.0);
    double v = 0.0;
    std::memcpy(&v, buf->data + buf->position, 8);
    buf->position += 8;
    return v;
}

int32_t netudp_buffer_read_varint(netudp_buffer_t* buf) {
    if (buf == nullptr) {
        return 0;
    }
    uint32_t result = 0;
    int shift = 0;
    while (buf->position < buf->capacity) {
        uint8_t byte = buf->data[buf->position++];
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            break;
        }
        shift += 7;
        if (shift >= 35) {
            break;
        }
    }
    return static_cast<int32_t>(result);
}

#undef BUF_CHECK
#undef BUF_CHECK_R

/* Token generation — implemented in connect_token.cpp, wired here */

int netudp_generate_connect_token(
    int num_server_addresses, const char** server_addresses,
    int expire_seconds, int timeout_seconds,
    uint64_t client_id, uint64_t protocol_id,
    const uint8_t private_key[32], uint8_t user_data[256],
    uint8_t connect_token[2048]) {

    if (num_server_addresses < 1 || num_server_addresses > NETUDP_MAX_SERVERS_PER_TOKEN) {
        return NETUDP_ERROR_INVALID_PARAM;
    }
    if (server_addresses == nullptr || private_key == nullptr || connect_token == nullptr) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    /* Parse server addresses */
    netudp::PrivateConnectToken priv = {};
    priv.client_id = client_id;
    priv.timeout_seconds = static_cast<uint32_t>(timeout_seconds);
    priv.num_server_addresses = static_cast<uint32_t>(num_server_addresses);

    for (int i = 0; i < num_server_addresses; ++i) {
        if (netudp_parse_address(server_addresses[i], &priv.server_addresses[i]) != NETUDP_OK) {
            return NETUDP_ERROR_INVALID_PARAM;
        }
    }

    /* Generate keys */
    netudp::crypto::random_bytes(priv.client_to_server_key, 32);
    netudp::crypto::random_bytes(priv.server_to_client_key, 32);

    /* Copy user data */
    if (user_data != nullptr) {
        std::memcpy(priv.user_data, user_data, 256);
    }

    /* Serialize private data */
    uint8_t private_data[netudp::TOKEN_PRIVATE_SIZE];
    std::memset(private_data, 0, sizeof(private_data));
    int serialized = netudp::serialize_private_token(&priv, private_data, netudp::TOKEN_PRIVATE_SIZE);
    if (serialized < 0) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    /* Build output token */
    std::memset(connect_token, 0, 2048);
    int pos = 0;

    /* version_info (13) */
    std::memcpy(connect_token + pos, NETUDP_VERSION_INFO, NETUDP_VERSION_INFO_BYTES);
    pos += NETUDP_VERSION_INFO_BYTES;

    /* protocol_id (8) */
    std::memcpy(connect_token + pos, &protocol_id, 8);
    pos += 8;

    /* create_timestamp (8) */
    uint64_t create_ts = static_cast<uint64_t>(std::time(nullptr));
    std::memcpy(connect_token + pos, &create_ts, 8);
    pos += 8;

    /* expire_timestamp (8) */
    uint64_t expire_ts = create_ts + static_cast<uint64_t>(expire_seconds);
    std::memcpy(connect_token + pos, &expire_ts, 8);
    pos += 8;

    /* nonce (24, random) */
    uint8_t nonce[24];
    netudp::crypto::random_bytes(nonce, 24);
    std::memcpy(connect_token + pos, nonce, 24);
    pos += 24;

    /* Build AAD: version(13) + protocol_id(8) + expire_ts(8) = 29 bytes */
    uint8_t aad[29];
    std::memcpy(aad, connect_token + netudp::TOKEN_VERSION_OFFSET, 13);
    std::memcpy(aad + 13, connect_token + netudp::TOKEN_PROTOCOL_ID_OFFSET, 8);
    std::memcpy(aad + 21, connect_token + netudp::TOKEN_EXPIRE_TS_OFFSET, 8);

    /* Encrypt private data → token[61..1100] (1024 + 16 = 1040) */
    int enc_len = netudp::crypto::xchacha_encrypt(
        private_key, nonce, aad, 29,
        private_data, netudp::TOKEN_PRIVATE_SIZE,
        connect_token + pos
    );
    if (enc_len < 0) {
        return NETUDP_ERROR_CRYPTO;
    }
    pos += enc_len;

    /* timeout_seconds (4) */
    uint32_t timeout_u32 = static_cast<uint32_t>(timeout_seconds);
    std::memcpy(connect_token + pos, &timeout_u32, 4);
    pos += 4;

    /* num_server_addresses (4) */
    uint32_t num_addrs = static_cast<uint32_t>(num_server_addresses);
    std::memcpy(connect_token + pos, &num_addrs, 4);
    pos += 4;

    /* server addresses (public portion — for client to read) */
    for (int i = 0; i < num_server_addresses; ++i) {
        uint8_t addr_buf[20];
        int addr_len = netudp::write_address_to_token(&priv.server_addresses[i], addr_buf);
        std::memcpy(connect_token + pos, addr_buf, static_cast<size_t>(addr_len));
        pos += addr_len;
    }

    /* c2s_key (32) — public, for client */
    std::memcpy(connect_token + pos, priv.client_to_server_key, 32);
    pos += 32;

    /* s2c_key (32) — public, for client */
    std::memcpy(connect_token + pos, priv.server_to_client_key, 32);
    pos += 32;

    /* Wipe sensitive data */
    crypto_wipe(private_data, sizeof(private_data));

    return NETUDP_OK;
}

} /* extern "C" */
