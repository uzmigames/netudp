#include "packet_crypto.h"
#include "aead.h"

namespace netudp {
namespace crypto {

int packet_encrypt(KeyEpoch* epoch, uint64_t protocol_id, uint8_t prefix_byte,
                   const uint8_t* pt, int pt_len, uint8_t* ct) {
    if (epoch == nullptr || pt == nullptr || ct == nullptr || pt_len < 0) {
        return -1;
    }

    uint8_t nonce[24];
    build_nonce(epoch->tx_nonce_counter, nonce);

    uint8_t aad[22];
    int aad_len = build_aad(protocol_id, prefix_byte, aad);

    int ct_len = aead_encrypt(epoch->tx_key, nonce, aad, aad_len, pt, pt_len, ct);
    if (ct_len < 0) {
        return -1;
    }

    epoch->tx_nonce_counter++;
    epoch->bytes_transmitted += static_cast<uint64_t>(pt_len);

    return ct_len;
}

int packet_decrypt(KeyEpoch* epoch, uint64_t protocol_id, uint8_t prefix_byte,
                   uint64_t nonce_counter, const uint8_t* ct, int ct_len, uint8_t* pt) {
    if (epoch == nullptr || ct == nullptr || pt == nullptr || ct_len < 16) {
        return -1;
    }

    /* Replay check BEFORE attempting decryption */
    if (epoch->replay.is_duplicate(nonce_counter)) {
        return -1;
    }

    uint8_t nonce[24];
    build_nonce(nonce_counter, nonce);

    uint8_t aad[22];
    int aad_len = build_aad(protocol_id, prefix_byte, aad);

    int pt_len = aead_decrypt(epoch->rx_key, nonce, aad, aad_len, ct, ct_len, pt);
    if (pt_len < 0) {
        return -1; /* Auth failure */
    }

    /* Only mark as received AFTER successful decryption */
    epoch->replay.advance(nonce_counter);

    return pt_len;
}

} // namespace crypto
} // namespace netudp
