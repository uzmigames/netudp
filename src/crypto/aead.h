#ifndef NETUDP_AEAD_H
#define NETUDP_AEAD_H

/**
 * @file aead.h
 * @brief ChaCha20-Poly1305 AEAD encrypt/decrypt wrappers.
 */

#include <cstdint>

namespace netudp {
namespace crypto {

/**
 * Encrypt plaintext with associated data using ChaCha20-Poly1305.
 * @param key      32-byte encryption key
 * @param nonce    12-byte nonce (must be unique per key)
 * @param aad      Associated data (authenticated but not encrypted)
 * @param aad_len  Length of AAD
 * @param pt       Plaintext input
 * @param pt_len   Plaintext length
 * @param ct       Output: ciphertext (must have capacity pt_len + 16)
 * @return         Ciphertext length (pt_len + 16) on success
 */
int aead_encrypt(const uint8_t key[32], const uint8_t nonce[24],
                 const uint8_t* aad, int aad_len,
                 const uint8_t* pt, int pt_len,
                 uint8_t* ct);

/**
 * Decrypt and verify ciphertext with associated data.
 * @param key      32-byte decryption key
 * @param nonce    12-byte nonce
 * @param aad      Associated data
 * @param aad_len  Length of AAD
 * @param ct       Ciphertext input (includes 16-byte Poly1305 tag at end)
 * @param ct_len   Ciphertext length (must be >= 16)
 * @param pt       Output: plaintext (must have capacity ct_len - 16)
 * @return         Plaintext length (ct_len - 16) on success, -1 on auth failure
 */
int aead_decrypt(const uint8_t key[32], const uint8_t nonce[24],
                 const uint8_t* aad, int aad_len,
                 const uint8_t* ct, int ct_len,
                 uint8_t* pt);

} // namespace crypto
} // namespace netudp

#endif /* NETUDP_AEAD_H */
