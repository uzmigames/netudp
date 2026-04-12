# Proposal: phase15_aes-gcm-fast-path

## Why

Monocypher's XChaCha20-Poly1305 costs 948ns per packet. AES-256-GCM with AES-NI hardware acceleration achieves ~400ns per packet — 2.4x faster. After frame coalescing and io_uring reduce syscall dominance, crypto becomes ~50% of per-packet cost, making AES-GCM a meaningful 29% total improvement. Offered as opt-in because AES-GCM fails catastrophically on nonce reuse, while XChaCha20 has a 2^192 nonce space.

Source: `docs/analysis/performance/04-encryption-analysis.md`, `07-optimization-roadmap.md` (Phase C)

## What Changes

- Runtime CPUID detection for AES-NI (`NETUDP_HAVE_AES_NI`)
- New AEAD backend: `src/crypto/aead_aesni.cpp` using AES-256-GCM intrinsics
- Dispatch: use AES-GCM if AES-NI detected AND user opts in, else keep XChaCha20
- Config flag: `netudp_server_config_t::crypto_mode` (default: XCHACHA20, opt-in: AES_GCM)
- XChaCha20-Poly1305 remains the default for its nonce-misuse resistance

## Impact

- Affected specs: crypto layer (new backend, existing API unchanged)
- Affected code: `src/crypto/aead.h`, new `src/crypto/aead_aesni.cpp`, `src/core/platform.h`
- Breaking change: NO (opt-in, XChaCha20 remains default)
- User benefit: 2x crypto throughput on AES-NI CPUs (948ns to ~400ns per packet)
