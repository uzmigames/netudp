#include "xchacha.h"
#include "vendor/monocypher.h"
#include "../profiling/profiler.h"

namespace netudp::crypto {

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
    NETUDP_ZONE("crypto::encrypt");
    if (pt_len < 0) {
        return -1;
    }

    /*
     * Monocypher's crypto_aead_lock takes a 24-byte nonce and internally
     * performs XChaCha20 key derivation (HChaCha20 + ChaCha20-Poly1305).
     * Pass the full 24-byte nonce directly — no manual subkey derivation needed.
     */
    {
        NETUDP_ZONE("crypto::aead_lock");
        uint8_t* mac = ct + pt_len;
        crypto_aead_lock(ct, mac, key, nonce,
                         aad, static_cast<size_t>(aad_len),
                         pt, static_cast<size_t>(pt_len));
    }

    return pt_len + 16;
}

int xchacha_decrypt(const uint8_t key[32], const uint8_t nonce[24],
                    const uint8_t* aad, int aad_len,
                    const uint8_t* ct, int ct_len,
                    uint8_t* pt) {
    NETUDP_ZONE("crypto::decrypt");
    if (ct_len < 16) {
        return -1;
    }

    int pt_len = ct_len - 16;
    int result;
    {
        NETUDP_ZONE("crypto::aead_unlock");
        const uint8_t* mac = ct + pt_len;
        result = crypto_aead_unlock(pt, mac, key, nonce,
                                    aad, static_cast<size_t>(aad_len),
                                    ct, static_cast<size_t>(pt_len));
    }

    if (result != 0) {
        return -1;
    }

    return pt_len;
}

} // namespace netudp::crypto
