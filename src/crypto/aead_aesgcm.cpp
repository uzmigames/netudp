#include "aead_dispatch.h"
#include "../core/platform.h"
#include "../profiling/profiler.h"

#include <cstring>

/**
 * AES-256-GCM AEAD implementation.
 *
 * Windows: uses BCrypt API (available since Vista, already linked).
 * Non-Windows: not available — aead_dispatch_init() never selects this path.
 *
 * Nonce handling: netudp uses 24-byte nonces internally. AES-GCM requires
 * 12 bytes. We use the first 12 bytes of the 24-byte nonce — this is safe
 * because our nonce is a little-endian 64-bit counter padded with zeros,
 * so the first 12 bytes contain the full counter value.
 */

namespace netudp::crypto {

/* AES-GCM tag size (always 16 bytes, same as Poly1305) */
static constexpr int kGcmTagSize = 16;

#ifdef NETUDP_PLATFORM_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

int aesgcm_encrypt(const uint8_t key[32], const uint8_t nonce[24],
                    const uint8_t* aad, int aad_len,
                    const uint8_t* pt, int pt_len,
                    uint8_t* ct) {
    NETUDP_ZONE("aesgcm::encrypt");
    if (pt_len < 0) { return -1; }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    NTSTATUS status = 0;

    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(status)) { return -1; }

    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                               reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                               static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)), 0);
    if (!NT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return -1; }

    status = BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                         const_cast<PUCHAR>(key), 32, 0);
    if (!NT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return -1; }

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
    status = BCryptEncrypt(hKey, const_cast<PUCHAR>(pt), static_cast<ULONG>(pt_len),
                            &authInfo, nullptr, 0,
                            ct, static_cast<ULONG>(pt_len), &ct_written, 0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!NT_SUCCESS(status)) { return -1; }
    return pt_len + kGcmTagSize;
}

int aesgcm_decrypt(const uint8_t key[32], const uint8_t nonce[24],
                    const uint8_t* aad, int aad_len,
                    const uint8_t* ct, int ct_len,
                    uint8_t* pt) {
    NETUDP_ZONE("aesgcm::decrypt");
    if (ct_len < kGcmTagSize) { return -1; }

    int pt_len = ct_len - kGcmTagSize;
    const uint8_t* mac = ct + pt_len;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    NTSTATUS status = 0;

    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(status)) { return -1; }

    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                               reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                               static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)), 0);
    if (!NT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return -1; }

    status = BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                         const_cast<PUCHAR>(key), 32, 0);
    if (!NT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return -1; }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(nonce);
    authInfo.cbNonce = 12;
    authInfo.pbAuthData = const_cast<PUCHAR>(aad);
    authInfo.cbAuthData = static_cast<ULONG>(aad_len);
    authInfo.pbTag = const_cast<PUCHAR>(mac);
    authInfo.cbTag = kGcmTagSize;

    ULONG pt_written = 0;
    status = BCryptDecrypt(hKey, const_cast<PUCHAR>(ct), static_cast<ULONG>(pt_len),
                            &authInfo, nullptr, 0,
                            pt, static_cast<ULONG>(pt_len), &pt_written, 0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!NT_SUCCESS(status)) { return -1; } /* Authentication failure */
    return pt_len;
}

#else /* Non-Windows (Linux / macOS) */

/*
 * On Linux/macOS, AES-256-GCM requires either OpenSSL or raw AES-NI
 * intrinsics (PCLMULQDQ + AESENC). The dispatch layer in aead_dispatch_init()
 * only selects aesgcm_encrypt/decrypt on Windows where BCrypt is available.
 * On non-Windows, cpu_has_aesni() returns true but the dispatch checks
 * NETUDP_PLATFORM_WINDOWS before switching — so these functions are only
 * reachable if someone manually forces the dispatch. They return -1 to
 * signal the error clearly rather than silently corrupting data.
 */
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
