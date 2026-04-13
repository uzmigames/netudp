#include "packet_crypto.h"
#include "aead.h"
#include "aead_dispatch.h"
#include "vendor/monocypher.h"
#include "../core/log.h"
#include "../profiling/profiler.h"

#include <cinttypes>
#include <cstring>

namespace netudp::crypto {

/* ======================================================================
 * Key derivation helper — Blake2b-keyed PRF (replaces HKDF-SHA256)
 *
 * info = 0x02 || epoch_le32 (4 bytes) || role (1 byte)   [17 bytes total]
 * new_key = Blake2b_keyed(key=old_key[32], msg=info[17], hash_size=32)
 *
 * The label bytes are literal octets (not a C string) to avoid the
 * bugprone-not-null-terminated-result warning from memcpy(dest, string, n).
 * ====================================================================== */

static const uint8_t kRekeyLabel[12] = {
    0x6EU, 0x65U, 0x74U, 0x75U, 0x64U, 0x70U,  /* n e t u d p */
    0x2DU, 0x72U, 0x65U, 0x6BU, 0x65U, 0x79U   /* - r e k e y */
};

static void derive_key(const uint8_t* old_key, uint32_t epoch_number, uint8_t* new_key) {
    NETUDP_ZONE("crypto::derive_key");
    uint8_t info[16];
    std::memcpy(info, kRekeyLabel, 12);
    std::memcpy(info + 12, &epoch_number, 4);
    crypto_blake2b_keyed(new_key, 32, old_key, 32, info, 16);
    crypto_wipe(info, sizeof(info));
}

/* ======================================================================
 * Rekey implementation
 * ====================================================================== */

bool should_rekey(const KeyEpoch& epoch, double current_time) {
    NETUDP_ZONE("crypto::should_rekey");
    if (epoch.tx_nonce_counter >= REKEY_NONCE_THRESHOLD) {
        NLOG_DEBUG("[netudp] crypto: rekey triggered by nonce counter (%" PRIu64 ")",
                   epoch.tx_nonce_counter);
        return true;
    }
    if (epoch.bytes_transmitted >= REKEY_BYTES_THRESHOLD) {
        NLOG_DEBUG("[netudp] crypto: rekey triggered by bytes transmitted (%" PRIu64 ")",
                   epoch.bytes_transmitted);
        return true;
    }
    if (epoch.epoch_start_time > 0.0 &&
        (current_time - epoch.epoch_start_time) >= REKEY_EPOCH_SECONDS) {
        NLOG_DEBUG("[netudp] crypto: rekey triggered by epoch age (%.1fs)",
                   current_time - epoch.epoch_start_time);
        return true;
    }
    return false;
}

/*
 * Workflow for the REKEY initiator:
 *   1. prepare_rekey(epoch) — saves old_rx_key for grace, sets rekey_pending
 *                             Does NOT change tx_key/rx_key yet.
 *   2. packet_encrypt(epoch, ..., PACKET_PREFIX_DATA_REKEY, ...) — encrypts
 *                             with the OLD tx_key (still in epoch.tx_key).
 *   3. activate_rekey(epoch, time) — derives new tx/rx from old, switches,
 *                             resets counters, arms grace window.
 */

void prepare_rekey(KeyEpoch& epoch) {
    NETUDP_ZONE("crypto::prepare_rekey");
    /* Save current rx_key so grace window can still decrypt old-key packets */
    std::memcpy(epoch.old_rx_key, epoch.rx_key, 32);
    epoch.rekey_pending = true;
    NLOG_DEBUG("[netudp] crypto: rekey prepared (epoch=%u)", epoch.epoch_number);
    /* tx_key / rx_key are intentionally left unchanged here so the caller
     * can encrypt the REKEY-flagged packet with the old tx_key.           */
}

void activate_rekey(KeyEpoch& epoch, double current_time) {
    NETUDP_ZONE("crypto::activate_rekey");
    uint32_t next_epoch = epoch.epoch_number + 1U;

    /* Derive new keys from old keys */
    uint8_t new_tx[32];
    uint8_t new_rx[32];
    derive_key(epoch.tx_key, next_epoch, new_tx);
    derive_key(epoch.rx_key, next_epoch, new_rx);

    /* Switch to new keys */
    std::memcpy(epoch.tx_key, new_tx, 32);
    std::memcpy(epoch.rx_key, new_rx, 32);

    /* Reset per-epoch counters */
    epoch.tx_nonce_counter = 0;
    epoch.bytes_transmitted = 0;
    epoch.epoch_start_time = current_time;
    epoch.epoch_number = next_epoch;
    epoch.grace_packets_remaining = REKEY_GRACE_PACKETS;
    epoch.rekey_pending = false;

    /* Fresh replay window for new epoch */
    epoch.replay.reset();

    NLOG_DEBUG("[netudp] crypto: epoch activated (epoch=%u)", next_epoch);

    crypto_wipe(new_tx, 32);
    crypto_wipe(new_rx, 32);
}

void on_receive_rekey(KeyEpoch& epoch, double current_time) {
    NETUDP_ZONE("crypto::on_receive_rekey");
    NLOG_DEBUG("[netudp] crypto: rekey received (epoch=%u → %u)", epoch.epoch_number, epoch.epoch_number + 1U);
    /* Save old rx_key for grace window (receiver may get stale old-key packets) */
    std::memcpy(epoch.old_rx_key, epoch.rx_key, 32);
    /* Derive new keys using the same formula as the sender */
    activate_rekey(epoch, current_time);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — inflated by NLOG macro expansions
int packet_decrypt_grace(KeyEpoch* epoch, uint64_t protocol_id, uint8_t prefix_byte,
                         uint64_t nonce_counter, const uint8_t* ct, int ct_len,
                         uint8_t* pt) {
    NETUDP_ZONE("crypto::decrypt_grace");
    if (epoch == nullptr || ct == nullptr || pt == nullptr || ct_len < 16) {
        return -1;
    }
    if (epoch->grace_packets_remaining <= 0) {
        return -1;
    }

    uint8_t nonce[24];
    build_nonce(nonce_counter, nonce);

    uint8_t aad[22];
    int aad_len = build_aad(protocol_id, prefix_byte, aad);

    int pt_len = g_aead_decrypt(epoch->old_rx_key, nonce, aad, aad_len,
                              ct, ct_len, pt);
    if (pt_len >= 0) {
        --epoch->grace_packets_remaining;
        NLOG_TRACE("[netudp] crypto: grace decrypt ok (remaining=%d)", epoch->grace_packets_remaining);
        if (epoch->grace_packets_remaining == 0) {
            /* Grace window exhausted — zero old key material */
            NLOG_DEBUG("[netudp] crypto: grace window exhausted");
            crypto_wipe(epoch->old_rx_key, 32);
        }
    } else {
        NLOG_TRACE("[netudp] crypto: grace decrypt failed");
    }
    return pt_len;
}

int packet_encrypt(KeyEpoch* epoch, uint64_t protocol_id, uint8_t prefix_byte,
                   const uint8_t* pt, int pt_len, uint8_t* ct) {
    NETUDP_ZONE("crypto::packet_encrypt");
    if (epoch == nullptr || pt == nullptr || ct == nullptr || pt_len < 0) {
        return -1;
    }

    uint8_t nonce[24];
    build_nonce(epoch->tx_nonce_counter, nonce);

    uint8_t aad[22];
    int aad_len = build_aad(protocol_id, prefix_byte, aad);

    int ct_len = g_aead_encrypt(epoch->tx_key, nonce, aad, aad_len, pt, pt_len, ct);
    if (ct_len < 0) {
        return -1;
    }

    epoch->tx_nonce_counter++;
    epoch->bytes_transmitted += static_cast<uint64_t>(pt_len);

    return ct_len;
}

int packet_decrypt(KeyEpoch* epoch, uint64_t protocol_id, uint8_t prefix_byte,
                   uint64_t nonce_counter, const uint8_t* ct, int ct_len, uint8_t* pt) {
    NETUDP_ZONE("crypto::packet_decrypt");
    if (epoch == nullptr || ct == nullptr || pt == nullptr || ct_len < 16) {
        return -1;
    }

    /* Replay check BEFORE attempting decryption */
    if (epoch->replay.is_duplicate(nonce_counter)) {
        NLOG_TRACE("[netudp] crypto: replay detected (nonce=%" PRIu64 ")", nonce_counter);
        return -1;
    }

    uint8_t nonce[24];
    build_nonce(nonce_counter, nonce);

    uint8_t aad[22];
    int aad_len = build_aad(protocol_id, prefix_byte, aad);

    int pt_len = g_aead_decrypt(epoch->rx_key, nonce, aad, aad_len, ct, ct_len, pt);
    if (pt_len < 0) {
        return -1; /* Auth failure */
    }

    /* Only mark as received AFTER successful decryption */
    epoch->replay.advance(nonce_counter);

    return pt_len;
}

} // namespace netudp::crypto
