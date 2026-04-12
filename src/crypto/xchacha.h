#ifndef NETUDP_XCHACHA_H
#define NETUDP_XCHACHA_H

/**
 * @file xchacha.h
 * @brief XChaCha20-Poly1305 for connect token encryption (24-byte nonce).
 */

#include <cstdint>

namespace netudp {
namespace crypto {

int xchacha_encrypt(const uint8_t key[32], const uint8_t nonce[24],
                    const uint8_t* aad, int aad_len,
                    const uint8_t* pt, int pt_len,
                    uint8_t* ct);

int xchacha_decrypt(const uint8_t key[32], const uint8_t nonce[24],
                    const uint8_t* aad, int aad_len,
                    const uint8_t* ct, int ct_len,
                    uint8_t* pt);

} // namespace crypto
} // namespace netudp

#endif /* NETUDP_XCHACHA_H */
