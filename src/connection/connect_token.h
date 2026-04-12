#ifndef NETUDP_CONNECT_TOKEN_H
#define NETUDP_CONNECT_TOKEN_H

/**
 * @file connect_token.h
 * @brief Connect token generation and validation (spec 05).
 *
 * Token layout (2048 bytes):
 *   Public:  version(13) + protocol_id(8) + create_ts(8) + expire_ts(8) +
 *            nonce(24) + encrypted_private(1024) + timeout(4) + num_addrs(4) +
 *            server_addresses(var) + c2s_key(32) + s2c_key(32) + padding
 *   Private: client_id(8) + timeout(4) + num_addrs(4) + addresses(var) +
 *            c2s_key(32) + s2c_key(32) + user_data(256) + padding to 1024
 */

#include <netudp/netudp_types.h>
#include <netudp/netudp_config.h>
#include <cstdint>

namespace netudp {

/* Offsets in public token */
static constexpr int TOKEN_VERSION_OFFSET      = 0;
static constexpr int TOKEN_PROTOCOL_ID_OFFSET  = 13;
static constexpr int TOKEN_CREATE_TS_OFFSET    = 21;
static constexpr int TOKEN_EXPIRE_TS_OFFSET    = 29;
static constexpr int TOKEN_NONCE_OFFSET        = 37;
static constexpr int TOKEN_PRIVATE_OFFSET      = 61;
static constexpr int TOKEN_PRIVATE_SIZE        = 1008; /* Plaintext (padded to fit MAC in 1024) */
static constexpr int TOKEN_PRIVATE_ENCRYPTED_SIZE = 1024; /* = 1008 plaintext + 16 Poly1305 tag */
static constexpr int TOKEN_AFTER_PRIVATE       = TOKEN_PRIVATE_OFFSET + TOKEN_PRIVATE_ENCRYPTED_SIZE; /* 61 + 1024 = 1085 */
/* After private: timeout(4) + num_addrs(4) + addrs(var) + c2s_key(32) + s2c_key(32) + pad */

/* Max size of server address in token: type(1) + IPv6(16) + port(2) = 19 */
static constexpr int TOKEN_ADDR_SIZE_IPV4 = 7;  /* type(1) + ipv4(4) + port(2) */
static constexpr int TOKEN_ADDR_SIZE_IPV6 = 19; /* type(1) + ipv6(16) + port(2) */

struct PrivateConnectToken {
    uint64_t client_id;
    uint32_t timeout_seconds;
    uint32_t num_server_addresses;
    netudp_address_t server_addresses[NETUDP_MAX_SERVERS_PER_TOKEN];
    uint8_t  client_to_server_key[32];
    uint8_t  server_to_client_key[32];
    uint8_t  user_data[256];
};

/**
 * Serialize private token data into a buffer for encryption.
 * Returns bytes written, or -1 on error.
 */
int serialize_private_token(const PrivateConnectToken* token, uint8_t* buf, int buf_size);

/**
 * Deserialize private token data from a decrypted buffer.
 * Returns 0 on success, -1 on error.
 */
int deserialize_private_token(const uint8_t* buf, int buf_size, PrivateConnectToken* token);

/**
 * Validate a connect token on the server side.
 * Decrypts the private portion, checks expiry, verifies server address.
 * Returns 0 on success, error code on failure.
 */
int validate_connect_token(
    const uint8_t token[2048],
    uint64_t protocol_id,
    const uint8_t private_key[32],
    uint64_t current_time,
    const netudp_address_t* server_address,
    PrivateConnectToken* out
);

/** Write address in token format (type + data + port). Returns bytes written. */
int write_address_to_token(const netudp_address_t* addr, uint8_t* buf);

/* Token fingerprint for anti-replay (spec 05 REQ-05.4 step 9) */
struct TokenFingerprint {
    uint8_t hash[8];
};

/**
 * Compute token fingerprint: first 8 bytes of HMAC(private_key, encrypted_private_data).
 * Using monocypher's crypto_blake2b for simplicity (HMAC-like keyed hash).
 */
TokenFingerprint compute_token_fingerprint(
    const uint8_t private_key[32],
    const uint8_t* encrypted_private, int encrypted_len
);

} // namespace netudp

#endif /* NETUDP_CONNECT_TOKEN_H */
