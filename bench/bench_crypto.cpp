/**
 * @file bench_crypto.cpp
 * @brief Isolated AEAD crypto benchmark — all viable algorithms for game networking.
 *
 * Tests raw encrypt+decrypt throughput at realistic game packet sizes (64B, 256B,
 * 512B, 1200B) for each algorithm variant available in monocypher and BCrypt.
 *
 * Algorithms tested:
 *   1. XChaCha20-Poly1305 one-shot  (monocypher crypto_aead_lock/unlock)
 *   2. XChaCha20-Poly1305 streaming (monocypher crypto_aead_init_x + write/read)
 *   3. IETF ChaCha20-Poly1305 streaming (monocypher crypto_aead_init_ietf + write/read)
 *   4. AES-256-GCM per-call         (BCrypt — current impl, handle per call)
 *   5. AES-256-GCM cached handle    (BCrypt — pre-computed key schedule)
 *   6. CRC32C integrity-only        (no encryption, LAN-mode baseline)
 *   7. Raw memcpy                   (absolute floor — zero crypto overhead)
 */

#include "bench_main.h"

#include "../src/crypto/vendor/monocypher.h"
#include "../src/crypto/aead.h"
#include "../src/crypto/aead_dispatch.h"
#include "../src/crypto/crc32c.h"
#include "../src/crypto/random.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

/* -----------------------------------------------------------------------
 * Constants
 * --------------------------------------------------------------------- */

static constexpr int kPayloadSizes[] = { 64, 256, 512, 1200 };
static constexpr int kNumSizes       = sizeof(kPayloadSizes) / sizeof(kPayloadSizes[0]);
static constexpr int kOpsPerSample   = 10000; /* ops per timing sample */

/* Pre-generated key / nonce / AAD — same for all algorithms */
static uint8_t g_key[32];
static uint8_t g_nonce24[24];     /* 24-byte nonce (XChaCha20) */
static uint8_t g_nonce12[12];     /* 12-byte nonce (IETF ChaCha20, AES-GCM) */
static uint8_t g_aad[22];
static constexpr int kAadLen = 22;

/* Buffers: plaintext, ciphertext, decrypted */
static uint8_t g_pt[1400];
static uint8_t g_ct[1400 + 16];  /* room for 16-byte tag */
static uint8_t g_dec[1400];

static void init_test_data() {
    netudp::crypto::random_bytes(g_key, 32);
    netudp::crypto::random_bytes(g_nonce24, 24);
    std::memcpy(g_nonce12, g_nonce24, 12);
    netudp::crypto::random_bytes(g_aad, kAadLen);
    netudp::crypto::random_bytes(g_pt, sizeof(g_pt));
}

/* -----------------------------------------------------------------------
 * 1. XChaCha20-Poly1305 one-shot (current default)
 * --------------------------------------------------------------------- */

static BenchResult bench_xchacha20_oneshot(const BenchConfig& cfg, int payload_size) {
    BenchResult r;
    char name[80];
    std::snprintf(name, sizeof(name), "crypto/xchacha20_oneshot/%dB", payload_size);
    r.name = name;

    const int samples = std::max(cfg.measure_iters, 20);
    r.samples_ns.reserve(static_cast<size_t>(samples));

    for (int s = 0; s < samples; ++s) {
        auto t0 = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < kOpsPerSample; ++i) {
            /* Encrypt */
            uint8_t* mac = g_ct + payload_size;
            crypto_aead_lock(g_ct, mac, g_key, g_nonce24,
                             g_aad, kAadLen, g_pt, static_cast<size_t>(payload_size));

            /* Decrypt */
            crypto_aead_unlock(g_dec, mac, g_key, g_nonce24,
                               g_aad, kAadLen, g_ct, static_cast<size_t>(payload_size));
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double total_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        double per_op_ns = total_ns / static_cast<double>(kOpsPerSample);
        r.samples_ns.push_back(per_op_ns);
    }

    double median = bench_percentile(r.samples_ns, 50.0);
    r.ops_per_sec = (median > 0.0) ? 1e9 / median : 0.0;

    double throughput_mbps = (median > 0.0)
        ? (static_cast<double>(payload_size) * 2.0 * 1e9) / (median * 1024.0 * 1024.0)
        : 0.0;
    std::printf("          %dB encrypt+decrypt: p50=%.0f ns  (%.1f MB/s)\n",
                payload_size, median, throughput_mbps);

    return r;
}

/* -----------------------------------------------------------------------
 * 2. XChaCha20-Poly1305 streaming (pre-derived subkey)
 * --------------------------------------------------------------------- */

static BenchResult bench_xchacha20_stream(const BenchConfig& cfg, int payload_size) {
    BenchResult r;
    char name[80];
    std::snprintf(name, sizeof(name), "crypto/xchacha20_stream/%dB", payload_size);
    r.name = name;

    const int samples = std::max(cfg.measure_iters, 20);
    r.samples_ns.reserve(static_cast<size_t>(samples));

    for (int s = 0; s < samples; ++s) {
        /* Init streaming context OUTSIDE the timing loop (amortized per-connection) */
        crypto_aead_ctx enc_ctx, dec_ctx;
        crypto_aead_init_x(&enc_ctx, g_key, g_nonce24);
        crypto_aead_init_x(&dec_ctx, g_key, g_nonce24);

        auto t0 = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < kOpsPerSample; ++i) {
            uint8_t mac[16];
            crypto_aead_write(&enc_ctx, g_ct, mac,
                              g_aad, kAadLen, g_pt, static_cast<size_t>(payload_size));
            crypto_aead_read(&dec_ctx, g_dec, mac,
                             g_aad, kAadLen, g_ct, static_cast<size_t>(payload_size));
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double total_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        double per_op_ns = total_ns / static_cast<double>(kOpsPerSample);
        r.samples_ns.push_back(per_op_ns);
    }

    double median = bench_percentile(r.samples_ns, 50.0);
    r.ops_per_sec = (median > 0.0) ? 1e9 / median : 0.0;

    double throughput_mbps = (median > 0.0)
        ? (static_cast<double>(payload_size) * 2.0 * 1e9) / (median * 1024.0 * 1024.0)
        : 0.0;
    std::printf("          %dB encrypt+decrypt: p50=%.0f ns  (%.1f MB/s)\n",
                payload_size, median, throughput_mbps);

    return r;
}

/* -----------------------------------------------------------------------
 * 3. IETF ChaCha20-Poly1305 streaming (12-byte nonce, netcode.io style)
 * --------------------------------------------------------------------- */

static BenchResult bench_chacha20_ietf_stream(const BenchConfig& cfg, int payload_size) {
    BenchResult r;
    char name[80];
    std::snprintf(name, sizeof(name), "crypto/chacha20_ietf_stream/%dB", payload_size);
    r.name = name;

    const int samples = std::max(cfg.measure_iters, 20);
    r.samples_ns.reserve(static_cast<size_t>(samples));

    /* IETF nonce = 12 bytes; monocypher uses 8-byte nonce for init_ietf */
    for (int s = 0; s < samples; ++s) {
        crypto_aead_ctx enc_ctx, dec_ctx;
        crypto_aead_init_ietf(&enc_ctx, g_key, g_nonce12);
        crypto_aead_init_ietf(&dec_ctx, g_key, g_nonce12);

        auto t0 = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < kOpsPerSample; ++i) {
            uint8_t mac[16];
            crypto_aead_write(&enc_ctx, g_ct, mac,
                              g_aad, kAadLen, g_pt, static_cast<size_t>(payload_size));
            crypto_aead_read(&dec_ctx, g_dec, mac,
                             g_aad, kAadLen, g_ct, static_cast<size_t>(payload_size));
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double total_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        double per_op_ns = total_ns / static_cast<double>(kOpsPerSample);
        r.samples_ns.push_back(per_op_ns);
    }

    double median = bench_percentile(r.samples_ns, 50.0);
    r.ops_per_sec = (median > 0.0) ? 1e9 / median : 0.0;

    double throughput_mbps = (median > 0.0)
        ? (static_cast<double>(payload_size) * 2.0 * 1e9) / (median * 1024.0 * 1024.0)
        : 0.0;
    std::printf("          %dB encrypt+decrypt: p50=%.0f ns  (%.1f MB/s)\n",
                payload_size, median, throughput_mbps);

    return r;
}

/* -----------------------------------------------------------------------
 * 4. AES-256-GCM per-call (current BCrypt implementation)
 * --------------------------------------------------------------------- */

#ifdef _WIN32

static BenchResult bench_aesgcm_percall(const BenchConfig& cfg, int payload_size) {
    BenchResult r;
    char name[80];
    std::snprintf(name, sizeof(name), "crypto/aes256gcm_percall/%dB", payload_size);
    r.name = name;

    if (!netudp::crypto::cpu_has_aesni()) {
        std::printf("          SKIPPED — no AES-NI\n");
        return r;
    }

    const int samples = std::max(cfg.measure_iters, 20);
    r.samples_ns.reserve(static_cast<size_t>(samples));

    for (int s = 0; s < samples; ++s) {
        auto t0 = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < kOpsPerSample; ++i) {
            netudp::crypto::aesgcm_encrypt(g_key, g_nonce24,
                                            g_aad, kAadLen,
                                            g_pt, payload_size, g_ct);
            netudp::crypto::aesgcm_decrypt(g_key, g_nonce24,
                                            g_aad, kAadLen,
                                            g_ct, payload_size + 16, g_dec);
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double total_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        double per_op_ns = total_ns / static_cast<double>(kOpsPerSample);
        r.samples_ns.push_back(per_op_ns);
    }

    double median = bench_percentile(r.samples_ns, 50.0);
    r.ops_per_sec = (median > 0.0) ? 1e9 / median : 0.0;

    double throughput_mbps = (median > 0.0)
        ? (static_cast<double>(payload_size) * 2.0 * 1e9) / (median * 1024.0 * 1024.0)
        : 0.0;
    std::printf("          %dB encrypt+decrypt: p50=%.0f ns  (%.1f MB/s)\n",
                payload_size, median, throughput_mbps);

    return r;
}

/* -----------------------------------------------------------------------
 * 5. AES-256-GCM cached handle (pre-computed key schedule via BCrypt)
 * --------------------------------------------------------------------- */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

struct AesGcmCachedCtx {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    bool valid = false;

    bool init(const uint8_t key[32]) {
        NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
        if (!NT_SUCCESS(status)) return false;

        status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                                   reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                                   static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)), 0);
        if (!NT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }

        status = BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                             const_cast<PUCHAR>(key), 32, 0);
        if (!NT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }

        valid = true;
        return true;
    }

    int encrypt(const uint8_t nonce[24], const uint8_t* aad, int aad_len,
                const uint8_t* pt, int pt_len, uint8_t* ct) {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce    = const_cast<PUCHAR>(nonce);
        authInfo.cbNonce    = 12;
        authInfo.pbAuthData = const_cast<PUCHAR>(aad);
        authInfo.cbAuthData = static_cast<ULONG>(aad_len);

        uint8_t* mac    = ct + pt_len;
        authInfo.pbTag  = mac;
        authInfo.cbTag  = 16;

        ULONG written = 0;
        NTSTATUS status = BCryptEncrypt(hKey, const_cast<PUCHAR>(pt), static_cast<ULONG>(pt_len),
                                        &authInfo, nullptr, 0,
                                        ct, static_cast<ULONG>(pt_len), &written, 0);
        return NT_SUCCESS(status) ? pt_len + 16 : -1;
    }

    int decrypt(const uint8_t nonce[24], const uint8_t* aad, int aad_len,
                const uint8_t* ct, int ct_len, uint8_t* pt) {
        int pt_len = ct_len - 16;
        const uint8_t* mac = ct + pt_len;

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce    = const_cast<PUCHAR>(nonce);
        authInfo.cbNonce    = 12;
        authInfo.pbAuthData = const_cast<PUCHAR>(aad);
        authInfo.cbAuthData = static_cast<ULONG>(aad_len);
        authInfo.pbTag      = const_cast<PUCHAR>(mac);
        authInfo.cbTag      = 16;

        ULONG written = 0;
        NTSTATUS status = BCryptDecrypt(hKey, const_cast<PUCHAR>(ct), static_cast<ULONG>(pt_len),
                                        &authInfo, nullptr, 0,
                                        pt, static_cast<ULONG>(pt_len), &written, 0);
        return NT_SUCCESS(status) ? pt_len : -1;
    }

    void destroy() {
        if (hKey) { BCryptDestroyKey(hKey);  hKey = nullptr; }
        if (hAlg) { BCryptCloseAlgorithmProvider(hAlg, 0); hAlg = nullptr; }
        valid = false;
    }
};

static BenchResult bench_aesgcm_cached(const BenchConfig& cfg, int payload_size) {
    BenchResult r;
    char name[80];
    std::snprintf(name, sizeof(name), "crypto/aes256gcm_cached/%dB", payload_size);
    r.name = name;

    if (!netudp::crypto::cpu_has_aesni()) {
        std::printf("          SKIPPED — no AES-NI\n");
        return r;
    }

    AesGcmCachedCtx enc_ctx, dec_ctx;
    if (!enc_ctx.init(g_key) || !dec_ctx.init(g_key)) {
        std::printf("          SKIPPED — BCrypt init failed\n");
        enc_ctx.destroy(); dec_ctx.destroy();
        return r;
    }

    const int samples = std::max(cfg.measure_iters, 20);
    r.samples_ns.reserve(static_cast<size_t>(samples));

    for (int s = 0; s < samples; ++s) {
        auto t0 = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < kOpsPerSample; ++i) {
            enc_ctx.encrypt(g_nonce24, g_aad, kAadLen, g_pt, payload_size, g_ct);
            dec_ctx.decrypt(g_nonce24, g_aad, kAadLen, g_ct, payload_size + 16, g_dec);
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double total_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        double per_op_ns = total_ns / static_cast<double>(kOpsPerSample);
        r.samples_ns.push_back(per_op_ns);
    }

    enc_ctx.destroy();
    dec_ctx.destroy();

    double median = bench_percentile(r.samples_ns, 50.0);
    r.ops_per_sec = (median > 0.0) ? 1e9 / median : 0.0;

    double throughput_mbps = (median > 0.0)
        ? (static_cast<double>(payload_size) * 2.0 * 1e9) / (median * 1024.0 * 1024.0)
        : 0.0;
    std::printf("          %dB encrypt+decrypt: p50=%.0f ns  (%.1f MB/s)\n",
                payload_size, median, throughput_mbps);

    return r;
}

#endif /* _WIN32 */

/* -----------------------------------------------------------------------
 * 6. CRC32C integrity-only (no encryption — LAN mode baseline)
 * --------------------------------------------------------------------- */

static BenchResult bench_crc32c_only(const BenchConfig& cfg, int payload_size) {
    BenchResult r;
    char name[80];
    std::snprintf(name, sizeof(name), "crypto/crc32c_only/%dB", payload_size);
    r.name = name;

    const int samples = std::max(cfg.measure_iters, 20);
    r.samples_ns.reserve(static_cast<size_t>(samples));

    for (int s = 0; s < samples; ++s) {
        auto t0 = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < kOpsPerSample; ++i) {
            /* Simulate send: memcpy + CRC32C */
            std::memcpy(g_ct, g_pt, static_cast<size_t>(payload_size));
            volatile uint32_t crc = netudp::crypto::crc32c(g_ct, payload_size);
            (void)crc;

            /* Simulate recv: CRC32C verify + memcpy */
            volatile uint32_t crc2 = netudp::crypto::crc32c(g_ct, payload_size);
            (void)crc2;
            std::memcpy(g_dec, g_ct, static_cast<size_t>(payload_size));
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double total_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        double per_op_ns = total_ns / static_cast<double>(kOpsPerSample);
        r.samples_ns.push_back(per_op_ns);
    }

    double median = bench_percentile(r.samples_ns, 50.0);
    r.ops_per_sec = (median > 0.0) ? 1e9 / median : 0.0;

    double throughput_mbps = (median > 0.0)
        ? (static_cast<double>(payload_size) * 2.0 * 1e9) / (median * 1024.0 * 1024.0)
        : 0.0;
    std::printf("          %dB send+recv: p50=%.0f ns  (%.1f MB/s)\n",
                payload_size, median, throughput_mbps);

    return r;
}

/* -----------------------------------------------------------------------
 * 7. Raw memcpy (absolute floor — zero crypto overhead)
 * --------------------------------------------------------------------- */

static BenchResult bench_memcpy_baseline(const BenchConfig& cfg, int payload_size) {
    BenchResult r;
    char name[80];
    std::snprintf(name, sizeof(name), "crypto/memcpy_baseline/%dB", payload_size);
    r.name = name;

    const int samples = std::max(cfg.measure_iters, 20);
    r.samples_ns.reserve(static_cast<size_t>(samples));

    for (int s = 0; s < samples; ++s) {
        auto t0 = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < kOpsPerSample; ++i) {
            std::memcpy(g_ct, g_pt, static_cast<size_t>(payload_size));
            std::memcpy(g_dec, g_ct, static_cast<size_t>(payload_size));
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double total_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        double per_op_ns = total_ns / static_cast<double>(kOpsPerSample);
        r.samples_ns.push_back(per_op_ns);
    }

    double median = bench_percentile(r.samples_ns, 50.0);
    r.ops_per_sec = (median > 0.0) ? 1e9 / median : 0.0;

    double throughput_mbps = (median > 0.0)
        ? (static_cast<double>(payload_size) * 2.0 * 1e9) / (median * 1024.0 * 1024.0)
        : 0.0;
    std::printf("          %dB copy+copy: p50=%.0f ns  (%.1f MB/s)\n",
                payload_size, median, throughput_mbps);

    return r;
}

/* -----------------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------------- */

void register_crypto_bench(BenchRegistry& reg) {
    init_test_data();

    for (int si = 0; si < kNumSizes; ++si) {
        const int sz = kPayloadSizes[si];

        /* 1. XChaCha20-Poly1305 one-shot */
        reg.add(std::string("crypto/xchacha20_oneshot/") + std::to_string(sz) + "B",
                [sz](const BenchConfig& cfg) { return bench_xchacha20_oneshot(cfg, sz); });

        /* 2. XChaCha20-Poly1305 streaming */
        reg.add(std::string("crypto/xchacha20_stream/") + std::to_string(sz) + "B",
                [sz](const BenchConfig& cfg) { return bench_xchacha20_stream(cfg, sz); });

        /* 3. IETF ChaCha20-Poly1305 streaming */
        reg.add(std::string("crypto/chacha20_ietf_stream/") + std::to_string(sz) + "B",
                [sz](const BenchConfig& cfg) { return bench_chacha20_ietf_stream(cfg, sz); });

#ifdef _WIN32
        /* 4. AES-256-GCM per-call (current impl) */
        reg.add(std::string("crypto/aes256gcm_percall/") + std::to_string(sz) + "B",
                [sz](const BenchConfig& cfg) { return bench_aesgcm_percall(cfg, sz); });

        /* 5. AES-256-GCM cached handle */
        reg.add(std::string("crypto/aes256gcm_cached/") + std::to_string(sz) + "B",
                [sz](const BenchConfig& cfg) { return bench_aesgcm_cached(cfg, sz); });
#endif

        /* 6. CRC32C integrity-only */
        reg.add(std::string("crypto/crc32c_only/") + std::to_string(sz) + "B",
                [sz](const BenchConfig& cfg) { return bench_crc32c_only(cfg, sz); });

        /* 7. Raw memcpy baseline */
        reg.add(std::string("crypto/memcpy_baseline/") + std::to_string(sz) + "B",
                [sz](const BenchConfig& cfg) { return bench_memcpy_baseline(cfg, sz); });
    }
}
