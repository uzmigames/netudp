#include "aead.h"
#include "vendor/monocypher.h"
#include "../profiling/profiler.h"
#include <cstring>

namespace netudp {
namespace crypto {

int aead_encrypt(const uint8_t key[32], const uint8_t nonce[24],
                 const uint8_t* aad, int aad_len,
                 const uint8_t* pt, int pt_len,
                 uint8_t* ct) {
    NETUDP_ZONE("aead::encrypt");
    if (pt_len < 0) {
        return -1;
    }

    /* ct layout: [ciphertext (pt_len)] [mac (16)] */
    uint8_t* mac = ct + pt_len;

    crypto_aead_lock(ct, mac, key, nonce,
                     aad, static_cast<size_t>(aad_len),
                     pt, static_cast<size_t>(pt_len));

    return pt_len + 16;
}

int aead_decrypt(const uint8_t key[32], const uint8_t nonce[24],
                 const uint8_t* aad, int aad_len,
                 const uint8_t* ct, int ct_len,
                 uint8_t* pt) {
    NETUDP_ZONE("aead::decrypt");
    if (ct_len < 16) {
        return -1;
    }

    int pt_len = ct_len - 16;
    const uint8_t* mac = ct + pt_len;

    int result = crypto_aead_unlock(pt, mac, key, nonce,
                                     aad, static_cast<size_t>(aad_len),
                                     ct, static_cast<size_t>(pt_len));

    if (result != 0) {
        return -1; /* Authentication failure */
    }

    return pt_len;
}

} // namespace crypto
} // namespace netudp
