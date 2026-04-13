#ifndef NETUDP_AEAD_DISPATCH_H
#define NETUDP_AEAD_DISPATCH_H

/**
 * @file aead_dispatch.h
 * @brief Runtime AEAD algorithm dispatch.
 *
 * Selects between XChaCha20-Poly1305 (default) and AES-256-GCM (opt-in)
 * based on crypto_mode config and runtime AES-NI detection.
 *
 * The function pointers are set once at netudp_init() or server_create()
 * and remain constant for the lifetime of the process.
 */

#include <cstdint>

namespace netudp::crypto {

/** Function pointer type for AEAD encrypt. */
using AeadEncryptFn = int (*)(const uint8_t key[32], const uint8_t nonce[24],
                               const uint8_t* aad, int aad_len,
                               const uint8_t* pt, int pt_len,
                               uint8_t* ct);

/** Function pointer type for AEAD decrypt. */
using AeadDecryptFn = int (*)(const uint8_t key[32], const uint8_t nonce[24],
                               const uint8_t* aad, int aad_len,
                               const uint8_t* ct, int ct_len,
                               uint8_t* pt);

/** Global dispatch pointers — set at init, read-only after. */
extern AeadEncryptFn g_aead_encrypt;
extern AeadDecryptFn g_aead_decrypt;

/** Returns true if the CPU supports AES-NI instructions. */
bool cpu_has_aesni();

/**
 * Initialize AEAD dispatch based on requested crypto mode.
 * @param mode  NETUDP_CRYPTO_XCHACHA20 or NETUDP_CRYPTO_AES_GCM.
 *              AES_GCM falls back to XChaCha20 if AES-NI is not available.
 */
void aead_dispatch_init(int mode);

/* AES-256-GCM implementations (defined in aead_aesgcm.cpp) */
int aesgcm_encrypt(const uint8_t key[32], const uint8_t nonce[24],
                    const uint8_t* aad, int aad_len,
                    const uint8_t* pt, int pt_len,
                    uint8_t* ct);

int aesgcm_decrypt(const uint8_t key[32], const uint8_t nonce[24],
                    const uint8_t* aad, int aad_len,
                    const uint8_t* ct, int ct_len,
                    uint8_t* pt);

} // namespace netudp::crypto

#endif /* NETUDP_AEAD_DISPATCH_H */
