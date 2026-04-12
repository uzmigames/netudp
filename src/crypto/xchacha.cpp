#include "xchacha.h"
#include "vendor/monocypher.h"
#include <cstring>

namespace netudp {
namespace crypto {

/*
 * Monocypher provides crypto_aead_lock/unlock for ChaCha20-Poly1305 (12-byte nonce).
 * For XChaCha20 (24-byte nonce), we use HChaCha20 to derive a subkey, then use
 * the standard AEAD with the last 12 bytes of the 24-byte nonce.
 * Monocypher's crypto_aead_lock already supports this when using the
 * crypto_aead_init_x / ietf variant. However, the simpler approach is to do
 * HChaCha20 manually and call the standard AEAD.
 */

int xchacha_encrypt(const uint8_t key[32], const uint8_t nonce[24],
                    const uint8_t* aad, int aad_len,
                    const uint8_t* pt, int pt_len,
                    uint8_t* ct) {
    if (pt_len < 0) {
        return -1;
    }

    /* Derive subkey via HChaCha20 */
    uint8_t subkey[32];
    crypto_chacha20_h(subkey, key, nonce);

    /* Use last 12 bytes of 24-byte nonce as AEAD nonce, with first 4 bytes zeroed */
    uint8_t subnonce[12];
    std::memset(subnonce, 0, 4);
    std::memcpy(subnonce + 4, nonce + 16, 8);

    uint8_t* mac = ct + pt_len;
    crypto_aead_lock(ct, mac, subkey, subnonce,
                     aad, static_cast<size_t>(aad_len),
                     pt, static_cast<size_t>(pt_len));

    /* Wipe subkey */
    crypto_wipe(subkey, sizeof(subkey));

    return pt_len + 16;
}

int xchacha_decrypt(const uint8_t key[32], const uint8_t nonce[24],
                    const uint8_t* aad, int aad_len,
                    const uint8_t* ct, int ct_len,
                    uint8_t* pt) {
    if (ct_len < 16) {
        return -1;
    }

    int pt_len = ct_len - 16;

    uint8_t subkey[32];
    crypto_chacha20_h(subkey, key, nonce);

    uint8_t subnonce[12];
    std::memset(subnonce, 0, 4);
    std::memcpy(subnonce + 4, nonce + 16, 8);

    const uint8_t* mac = ct + pt_len;
    int result = crypto_aead_unlock(pt, mac, subkey, subnonce,
                                     aad, static_cast<size_t>(aad_len),
                                     ct, static_cast<size_t>(pt_len));

    crypto_wipe(subkey, sizeof(subkey));

    if (result != 0) {
        return -1;
    }

    return pt_len;
}

} // namespace crypto
} // namespace netudp
