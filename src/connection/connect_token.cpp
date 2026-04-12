#include "connect_token.h"
#include <netudp/netudp.h>
#include "../core/address.h"
#include "../crypto/xchacha.h"
#include "../crypto/random.h"
#include "../crypto/vendor/monocypher.h"

#include <cstring>
#include <ctime>

namespace netudp {

/* ===== Serialize address into token format ===== */

int write_address_to_token(const netudp_address_t* addr, uint8_t* buf) {
    int pos = 0;
    buf[pos++] = addr->type;
    if (addr->type == NETUDP_ADDRESS_IPV4) {
        std::memcpy(buf + pos, addr->data.ipv4, 4);
        pos += 4;
    } else if (addr->type == NETUDP_ADDRESS_IPV6) {
        std::memcpy(buf + pos, addr->data.ipv6, 16);
        pos += 16;
    }
    buf[pos++] = static_cast<uint8_t>(addr->port & 0xFF);
    buf[pos++] = static_cast<uint8_t>((addr->port >> 8) & 0xFF);
    return pos;
}

static int read_address(const uint8_t* buf, int avail, netudp_address_t* addr) {
    if (avail < 1) {
        return -1;
    }
    *addr = address_zero();
    int pos = 0;
    addr->type = buf[pos++];

    if (addr->type == NETUDP_ADDRESS_IPV4) {
        if (avail < 7) {
            return -1;
        }
        std::memcpy(addr->data.ipv4, buf + pos, 4);
        pos += 4;
    } else if (addr->type == NETUDP_ADDRESS_IPV6) {
        if (avail < 19) {
            return -1;
        }
        std::memcpy(addr->data.ipv6, buf + pos, 16);
        pos += 16;
    } else {
        return -1;
    }
    addr->port = static_cast<uint16_t>(buf[pos] | (buf[pos + 1] << 8));
    pos += 2;
    return pos;
}

/* ===== Serialization ===== */

int serialize_private_token(const PrivateConnectToken* token, uint8_t* buf, int buf_size) {
    if (token == nullptr || buf == nullptr || buf_size < TOKEN_PRIVATE_SIZE) {
        return -1;
    }

    std::memset(buf, 0, static_cast<size_t>(buf_size));
    int pos = 0;

    /* client_id (8) */
    std::memcpy(buf + pos, &token->client_id, 8);
    pos += 8;

    /* timeout_seconds (4) */
    std::memcpy(buf + pos, &token->timeout_seconds, 4);
    pos += 4;

    /* num_server_addresses (4) */
    std::memcpy(buf + pos, &token->num_server_addresses, 4);
    pos += 4;

    /* server addresses */
    for (uint32_t i = 0; i < token->num_server_addresses; ++i) {
        int written = write_address_to_token(&token->server_addresses[i], buf + pos);
        pos += written;
    }

    /* c2s_key (32) */
    std::memcpy(buf + pos, token->client_to_server_key, 32);
    pos += 32;

    /* s2c_key (32) */
    std::memcpy(buf + pos, token->server_to_client_key, 32);
    pos += 32;

    /* user_data (256) */
    std::memcpy(buf + pos, token->user_data, 256);
    pos += 256;

    if (pos > TOKEN_PRIVATE_SIZE) {
        return -1; /* Overflow */
    }

    return pos;
}

int deserialize_private_token(const uint8_t* buf, int buf_size, PrivateConnectToken* token) {
    if (buf == nullptr || token == nullptr || buf_size < TOKEN_PRIVATE_SIZE) {
        return -1;
    }

    std::memset(token, 0, sizeof(PrivateConnectToken));
    int pos = 0;

    std::memcpy(&token->client_id, buf + pos, 8);
    pos += 8;

    std::memcpy(&token->timeout_seconds, buf + pos, 4);
    pos += 4;

    std::memcpy(&token->num_server_addresses, buf + pos, 4);
    pos += 4;

    if (token->num_server_addresses == 0 || token->num_server_addresses > NETUDP_MAX_SERVERS_PER_TOKEN) {
        return -1;
    }

    for (uint32_t i = 0; i < token->num_server_addresses; ++i) {
        int read = read_address(buf + pos, buf_size - pos, &token->server_addresses[i]);
        if (read < 0) {
            return -1;
        }
        pos += read;
    }

    std::memcpy(token->client_to_server_key, buf + pos, 32);
    pos += 32;

    std::memcpy(token->server_to_client_key, buf + pos, 32);
    pos += 32;

    std::memcpy(token->user_data, buf + pos, 256);

    return 0;
}

/* ===== Validation ===== */

int validate_connect_token(
    const uint8_t token[2048],
    uint64_t protocol_id,
    const uint8_t private_key[32],
    uint64_t current_time,
    const netudp_address_t* server_address,
    PrivateConnectToken* out
) {
    if (token == nullptr || private_key == nullptr || out == nullptr) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    /* Check version */
    if (std::memcmp(token + TOKEN_VERSION_OFFSET, NETUDP_VERSION_INFO, NETUDP_VERSION_INFO_BYTES) != 0) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    /* Check protocol_id */
    uint64_t token_protocol_id = 0;
    std::memcpy(&token_protocol_id, token + TOKEN_PROTOCOL_ID_OFFSET, 8);
    if (token_protocol_id != protocol_id) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    /* Check expiry */
    uint64_t expire_ts = 0;
    std::memcpy(&expire_ts, token + TOKEN_EXPIRE_TS_OFFSET, 8);
    if (current_time >= expire_ts) {
        return NETUDP_ERROR_TIMEOUT; /* Token expired */
    }

    /* Build AAD: version + protocol_id + expire_timestamp */
    uint8_t aad[29]; /* 13 + 8 + 8 */
    std::memcpy(aad, token + TOKEN_VERSION_OFFSET, 13);
    std::memcpy(aad + 13, token + TOKEN_PROTOCOL_ID_OFFSET, 8);
    std::memcpy(aad + 21, token + TOKEN_EXPIRE_TS_OFFSET, 8);

    /* Decrypt private data */
    const uint8_t* nonce = token + TOKEN_NONCE_OFFSET;
    const uint8_t* encrypted = token + TOKEN_PRIVATE_OFFSET;

    uint8_t decrypted[TOKEN_PRIVATE_SIZE];
    int dec_len = crypto::xchacha_decrypt(
        private_key, nonce, aad, 29,
        encrypted, TOKEN_PRIVATE_ENCRYPTED_SIZE, decrypted
    );
    if (dec_len < 0) {
        return NETUDP_ERROR_CRYPTO;
    }

    /* Deserialize */
    if (deserialize_private_token(decrypted, TOKEN_PRIVATE_SIZE, out) != 0) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    /* Check server address is in token */
    if (server_address != nullptr) {
        bool found = false;
        for (uint32_t i = 0; i < out->num_server_addresses; ++i) {
            if (netudp_address_equal(server_address, &out->server_addresses[i])) {
                found = true;
                break;
            }
        }
        if (!found) {
            return NETUDP_ERROR_INVALID_PARAM;
        }
    }

    /* Wipe decrypted buffer */
    crypto_wipe(decrypted, sizeof(decrypted));

    return NETUDP_OK;
}

/* ===== Token Fingerprint ===== */

TokenFingerprint compute_token_fingerprint(
    const uint8_t private_key[32],
    const uint8_t* encrypted_private, int encrypted_len
) {
    TokenFingerprint fp = {};
    uint8_t hash[64]; /* blake2b output */

    crypto_blake2b_keyed(hash, 64,
                          private_key, 32,
                          encrypted_private, static_cast<size_t>(encrypted_len));

    std::memcpy(fp.hash, hash, 8);
    crypto_wipe(hash, sizeof(hash));
    return fp;
}

} // namespace netudp
