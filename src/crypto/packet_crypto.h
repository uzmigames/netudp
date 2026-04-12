#ifndef NETUDP_PACKET_CRYPTO_H
#define NETUDP_PACKET_CRYPTO_H

/**
 * @file packet_crypto.h
 * @brief Per-connection key epoch and packet-level encrypt/decrypt.
 *
 * Each connection has a KeyEpoch with separate Tx/Rx keys and a
 * monotonic 64-bit nonce counter. The nonce counter is used for
 * both AEAD nonce construction and replay protection.
 */

#include <netudp/netudp_config.h>
#include "replay.h"
#include <cstdint>
#include <cstring>

namespace netudp {
namespace crypto {

struct KeyEpoch {
    uint8_t  tx_key[32] = {};
    uint8_t  rx_key[32] = {};
    uint64_t tx_nonce_counter = 0;
    uint64_t bytes_transmitted = 0;
    double   epoch_start_time = 0.0;
    uint32_t epoch_number = 0;
    ReplayProtection replay;
};

/** Build a 24-byte nonce from a 64-bit counter (little-endian, zero-padded).
 *  Monocypher uses XChaCha20-Poly1305 with 24-byte nonces. */
inline void build_nonce(uint64_t counter, uint8_t nonce[24]) {
    std::memset(nonce, 0, 24);
    std::memcpy(nonce, &counter, 8);
}

/** Build the 22-byte AAD: version_info(13) + protocol_id(8) + prefix_byte(1). */
inline int build_aad(uint64_t protocol_id, uint8_t prefix_byte, uint8_t aad[22]) {
    std::memcpy(aad, NETUDP_VERSION_INFO, NETUDP_VERSION_INFO_BYTES);
    std::memcpy(aad + NETUDP_VERSION_INFO_BYTES, &protocol_id, 8);
    aad[NETUDP_VERSION_INFO_BYTES + 8] = prefix_byte;
    return 22;
}

/**
 * Encrypt a packet payload.
 * @param epoch       Key epoch (tx_key + nonce counter)
 * @param protocol_id Protocol ID for AAD
 * @param prefix_byte Packet prefix byte for AAD
 * @param pt          Plaintext payload
 * @param pt_len      Plaintext length
 * @param ct          Output ciphertext (must have capacity pt_len + 16)
 * @return            Ciphertext length (pt_len + 16), or -1 on error
 */
int packet_encrypt(KeyEpoch* epoch, uint64_t protocol_id, uint8_t prefix_byte,
                   const uint8_t* pt, int pt_len, uint8_t* ct);

/**
 * Decrypt a packet payload with replay protection.
 * @param epoch        Key epoch (rx_key + replay)
 * @param protocol_id  Protocol ID for AAD
 * @param prefix_byte  Packet prefix byte for AAD
 * @param nonce_counter The nonce counter from the packet (derived from wire sequence + epoch)
 * @param ct           Ciphertext (includes 16-byte tag)
 * @param ct_len       Ciphertext length
 * @param pt           Output plaintext (must have capacity ct_len - 16)
 * @return             Plaintext length, or -1 on auth/replay failure
 */
int packet_decrypt(KeyEpoch* epoch, uint64_t protocol_id, uint8_t prefix_byte,
                   uint64_t nonce_counter, const uint8_t* ct, int ct_len, uint8_t* pt);

} // namespace crypto
} // namespace netudp

#endif /* NETUDP_PACKET_CRYPTO_H */
