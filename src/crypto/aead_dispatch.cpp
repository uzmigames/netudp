#include "aead_dispatch.h"
#include "aead.h"
#include "../core/platform.h"
#include "../core/log.h"

#include <netudp/netudp_types.h>

#if defined(NETUDP_ARCH_X64) || defined(NETUDP_ARCH_X86)
#ifdef NETUDP_COMPILER_MSVC
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace netudp::crypto {

/* Default: XChaCha20-Poly1305 (monocypher) */
AeadEncryptFn g_aead_encrypt = aead_encrypt;
AeadDecryptFn g_aead_decrypt = aead_decrypt;

bool cpu_has_aesni() {
#if defined(NETUDP_ARCH_X64) || defined(NETUDP_ARCH_X86)
    int info[4] = {};
#ifdef NETUDP_COMPILER_MSVC
    __cpuid(info, 1);
#else
    __cpuid(1, info[0], info[1], info[2], info[3]);
#endif
    return (info[2] & (1 << 25)) != 0; /* ECX bit 25: AES-NI */
#else
    return false; /* ARM — no AES-NI (ARMv8 CE is a separate path) */
#endif
}

void aead_dispatch_init(int mode) {
    if (mode == NETUDP_CRYPTO_AES_GCM) {
#ifdef NETUDP_PLATFORM_WINDOWS
        /* BCrypt AES-GCM available on Windows Vista+ (always present) */
        if (cpu_has_aesni()) {
            g_aead_encrypt = aesgcm_encrypt;
            g_aead_decrypt = aesgcm_decrypt;
            NLOG_INFO("[netudp] crypto: AES-256-GCM selected (BCrypt + AES-NI)");
            return;
        }
        NLOG_WARN("[netudp] crypto: AES-GCM requested but AES-NI not available, using XChaCha20");
#else
        NLOG_WARN("[netudp] crypto: AES-GCM not available on this platform, using XChaCha20");
#endif
        g_aead_encrypt = aead_encrypt;
        g_aead_decrypt = aead_decrypt;
    } else {
        g_aead_encrypt = aead_encrypt;
        g_aead_decrypt = aead_decrypt;
        NLOG_INFO("[netudp] crypto: XChaCha20-Poly1305 selected (default)");
    }
}

} // namespace netudp::crypto
