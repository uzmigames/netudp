#include "aead_dispatch.h"
#include "../core/platform.h"
#include "../core/log.h"
#include "../profiling/profiler.h"

#include <cstring>

/**
 * AES-256-GCM AEAD implementation with cached BCrypt handles.
 *
 * Windows: uses BCrypt API (available since Vista, already linked).
 * Non-Windows: not available — aead_dispatch_init() falls back to XChaCha20.
 *
 * Performance strategy:
 *   - Global BCrypt algorithm handle: opened once at aesgcm_init(), shared
 *     across all threads (BCrypt alg handles are thread-safe for read ops).
 *   - Thread-local key handle cache: each thread caches the last-used key
 *     and its BCrypt key handle. If the incoming key matches, the handle is
 *     reused (hot path: ~180 ns). On key mismatch, the old handle is
 *     destroyed and a new one created (~350 ns). Both are far cheaper than
 *     the previous per-call approach (~1,300 ns) which opened/closed the
 *     algorithm provider on every packet.
 *
 * Nonce handling: netudp uses 24-byte nonces internally. AES-GCM requires
 * 12 bytes. We use the first 12 bytes of the 24-byte nonce — this is safe
 * because our nonce is a little-endian 64-bit counter padded with zeros,
 * so the first 12 bytes contain the full counter value.
 */

namespace netudp::crypto {

#ifdef NETUDP_PLATFORM_WINDOWS

/* AES-GCM tag size (always 16 bytes, same as Poly1305) */
static constexpr int kGcmTagSize = 16;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

/* -----------------------------------------------------------------------
 * Global algorithm handle (opened once, thread-safe for BCryptEncrypt/Decrypt)
 * --------------------------------------------------------------------- */

static BCRYPT_ALG_HANDLE g_hAlg = nullptr;

bool aesgcm_init() {
    if (g_hAlg != nullptr) {
        return true; /* already initialized */
    }

    NTSTATUS status = BCryptOpenAlgorithmProvider(&g_hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(status)) {
        NLOG_ERROR("[netudp] aesgcm_init: BCryptOpenAlgorithmProvider failed (0x%08lx)",
                   static_cast<unsigned long>(status));
        return false;
    }

    status = BCryptSetProperty(g_hAlg, BCRYPT_CHAINING_MODE,
                               reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                               static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)), 0);
    if (!NT_SUCCESS(status)) {
        NLOG_ERROR("[netudp] aesgcm_init: BCryptSetProperty GCM failed (0x%08lx)",
                   static_cast<unsigned long>(status));
        BCryptCloseAlgorithmProvider(g_hAlg, 0);
        g_hAlg = nullptr;
        return false;
    }

    NLOG_INFO("[netudp] aesgcm_init: BCrypt AES-GCM algorithm handle cached");
    return true;
}

void aesgcm_term() {
    if (g_hAlg != nullptr) {
        BCryptCloseAlgorithmProvider(g_hAlg, 0);
        g_hAlg = nullptr;
    }
}

/* -----------------------------------------------------------------------
 * Thread-local key handle cache
 *
 * Each thread caches the last-used key and its BCrypt key handle.
 * Game servers typically process batches from the same connection,
 * so the cached key is reused for the vast majority of calls.
 * --------------------------------------------------------------------- */

struct TlsKeyCache {
    BCRYPT_KEY_HANDLE hKey = nullptr;
    uint8_t           key[32] = {};
    bool              valid = false;

    BCRYPT_KEY_HANDLE get(const uint8_t incoming_key[32]) {
        if (valid && std::memcmp(key, incoming_key, 32) == 0) {
            return hKey; /* cache hit */
        }
        /* Cache miss — destroy old handle and create new */
        if (hKey != nullptr) {
            BCryptDestroyKey(hKey);
            hKey = nullptr;
        }
        valid = false;

        NTSTATUS status = BCryptGenerateSymmetricKey(
            g_hAlg, &hKey, nullptr, 0,
            const_cast<PUCHAR>(incoming_key), 32, 0);
        if (!NT_SUCCESS(status)) {
            hKey = nullptr;
            return nullptr;
        }

        std::memcpy(key, incoming_key, 32);
        valid = true;
        return hKey;
    }

    ~TlsKeyCache() {
        if (hKey != nullptr) {
            BCryptDestroyKey(hKey);
        }
    }
};

static thread_local TlsKeyCache tls_encrypt_cache;
static thread_local TlsKeyCache tls_decrypt_cache;

/* -----------------------------------------------------------------------
 * Encrypt / Decrypt
 * --------------------------------------------------------------------- */

int aesgcm_encrypt(const uint8_t key[32], const uint8_t nonce[24],
                    const uint8_t* aad, int aad_len,
                    const uint8_t* pt, int pt_len,
                    uint8_t* ct) {
    NETUDP_ZONE("aesgcm::encrypt");
    if (pt_len < 0 || g_hAlg == nullptr) {
        return -1;
    }

    BCRYPT_KEY_HANDLE hKey = tls_encrypt_cache.get(key);
    if (hKey == nullptr) {
        return -1;
    }

    /* Use first 12 bytes of the 24-byte nonce for AES-GCM */
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(nonce);
    authInfo.cbNonce = 12;
    authInfo.pbAuthData = const_cast<PUCHAR>(aad);
    authInfo.cbAuthData = static_cast<ULONG>(aad_len);

    /* Tag goes after ciphertext (same layout as XChaCha20: [ct][mac16]) */
    uint8_t* mac = ct + pt_len;
    authInfo.pbTag = mac;
    authInfo.cbTag = kGcmTagSize;

    ULONG ct_written = 0;
    NTSTATUS status = BCryptEncrypt(hKey,
                                    const_cast<PUCHAR>(pt), static_cast<ULONG>(pt_len),
                                    &authInfo, nullptr, 0,
                                    ct, static_cast<ULONG>(pt_len), &ct_written, 0);

    if (!NT_SUCCESS(status)) {
        /* Key handle may be stale after BCrypt internal error — invalidate cache */
        tls_encrypt_cache.valid = false;
        return -1;
    }
    return pt_len + kGcmTagSize;
}

int aesgcm_decrypt(const uint8_t key[32], const uint8_t nonce[24],
                    const uint8_t* aad, int aad_len,
                    const uint8_t* ct, int ct_len,
                    uint8_t* pt) {
    NETUDP_ZONE("aesgcm::decrypt");
    if (ct_len < kGcmTagSize || g_hAlg == nullptr) {
        return -1;
    }

    BCRYPT_KEY_HANDLE hKey = tls_decrypt_cache.get(key);
    if (hKey == nullptr) {
        return -1;
    }

    int pt_len = ct_len - kGcmTagSize;
    const uint8_t* mac = ct + pt_len;

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(nonce);
    authInfo.cbNonce = 12;
    authInfo.pbAuthData = const_cast<PUCHAR>(aad);
    authInfo.cbAuthData = static_cast<ULONG>(aad_len);
    authInfo.pbTag = const_cast<PUCHAR>(mac);
    authInfo.cbTag = kGcmTagSize;

    ULONG pt_written = 0;
    NTSTATUS status = BCryptDecrypt(hKey,
                                    const_cast<PUCHAR>(ct), static_cast<ULONG>(pt_len),
                                    &authInfo, nullptr, 0,
                                    pt, static_cast<ULONG>(pt_len), &pt_written, 0);

    if (!NT_SUCCESS(status)) {
        /* Don't invalidate cache on auth failure — key handle is still valid,
         * the packet data was just bad (tampered, wrong key, replay, etc.) */
        return -1;
    }
    return pt_len;
}

#else /* Non-Windows (Linux / macOS) */

/*
 * On Linux/macOS, AES-256-GCM requires either OpenSSL or raw AES-NI
 * intrinsics (PCLMULQDQ + AESENC). The dispatch layer in aead_dispatch_init()
 * only selects aesgcm_encrypt/decrypt on Windows where BCrypt is available.
 * These stubs return -1 to signal the error clearly.
 */

bool aesgcm_init() { return false; }
void aesgcm_term() {}

int aesgcm_encrypt(const uint8_t key[32], const uint8_t nonce[24],
                    const uint8_t* aad, int aad_len,
                    const uint8_t* pt, int pt_len,
                    uint8_t* ct) {
    (void)key; (void)nonce; (void)aad; (void)aad_len;
    (void)pt; (void)pt_len; (void)ct;
    return -1;
}

int aesgcm_decrypt(const uint8_t key[32], const uint8_t nonce[24],
                    const uint8_t* aad, int aad_len,
                    const uint8_t* ct, int ct_len,
                    uint8_t* pt) {
    (void)key; (void)nonce; (void)aad; (void)aad_len;
    (void)ct; (void)ct_len; (void)pt;
    return -1;
}

#endif /* NETUDP_PLATFORM_WINDOWS */

} // namespace netudp::crypto
