# netudp — Crypto Algorithm Benchmark Report

**Date:** 2026-04-13
**Hardware:** i7-12700K, Windows 10, loopback
**Compiler:** Zig CC (Clang 20), Release build
**Measurement:** 10,000 ops/sample, 30 samples, 5 warmup iterations
**Source:** `bench/bench_crypto.cpp` — isolated AEAD encrypt+decrypt (no network I/O)

---

## 1. Algorithms Tested

| # | Algorithm | Library | Nonce | Notes |
|---|-----------|---------|------:|-------|
| 1 | XChaCha20-Poly1305 one-shot | Monocypher | 24B | Current default (`crypto_aead_lock/unlock`) |
| 2 | XChaCha20-Poly1305 streaming | Monocypher | 24B | Pre-derived subkey (`crypto_aead_init_x + write/read`) |
| 3 | ChaCha20-Poly1305 IETF streaming | Monocypher | 12B | Same as netcode.io (`crypto_aead_init_ietf + write/read`) |
| 4 | AES-256-GCM per-call | Windows BCrypt | 12B | Current opt-in (handle created/destroyed per call) |
| 5 | AES-256-GCM cached handle | Windows BCrypt | 12B | Pre-computed key schedule, handle reused per connection |
| 6 | CRC32C integrity-only | SIMD dispatch | N/A | No encryption, LAN-mode baseline |
| 7 | Raw memcpy | N/A | N/A | Absolute floor, zero crypto overhead |

---

## 2. Latency (p50 ns, encrypt + decrypt per operation)

| Algorithm | 64B | 256B | 512B | 1200B |
|-----------|----:|-----:|-----:|------:|
| **AES-256-GCM cached** | **176** | **313** | **352** | **746** |
| XChaCha20 streaming | 590 | 1,265 | 2,381 | 4,908 |
| ChaCha20-IETF streaming | 587 | 1,455 | 2,238 | 5,083 |
| XChaCha20 one-shot (current) | 776 | 1,476 | 2,543 | 5,029 |
| AES-256-GCM per-call (current) | 1,292 | 1,839 | 1,707 | 2,374 |
| CRC32C only (LAN) | 9 | 43 | 78 | 213 |
| memcpy baseline | 4 | 10 | 18 | 27 |

### Tail latency (1200B payload)

| Algorithm | p50 | p95 | p99 | max |
|-----------|----:|----:|----:|----:|
| AES-256-GCM cached | 746 | 791 | 798 | 863 |
| XChaCha20 streaming | 4,908 | 5,157 | 5,181 | 5,189 |
| XChaCha20 one-shot | 5,029 | 5,204 | 5,244 | 5,315 |
| ChaCha20-IETF streaming | 5,083 | 5,200 | 5,228 | 5,241 |
| AES-256-GCM per-call | 2,374 | 2,551 | 2,553 | 2,588 |

---

## 3. Throughput (MB/s, encrypt + decrypt)

| Algorithm | 64B | 256B | 512B | 1200B |
|-----------|----:|-----:|-----:|------:|
| **AES-256-GCM cached** | **680** | **1,557** | **2,778** | **3,009** |
| XChaCha20 streaming | 207 | 386 | 421 | 471 |
| ChaCha20-IETF streaming | 208 | 336 | 436 | 450 |
| XChaCha20 one-shot (current) | 156 | 331 | 384 | 455 |
| AES-256-GCM per-call (current) | 95 | 254 | 560 | 964 |
| CRC32C only (LAN) | 13,042 | 11,947 | 12,566 | 10,726 |
| memcpy baseline | 29,274 | 54,987 | 55,235 | 85,691 |

---

## 4. Speedup vs Current Default (XChaCha20 one-shot)

| Algorithm | 64B | 256B | 512B | 1200B |
|-----------|----:|-----:|-----:|------:|
| **AES-256-GCM cached** | **4.4x** | **4.7x** | **7.2x** | **6.7x** |
| XChaCha20 streaming | 1.32x | 1.17x | 1.07x | 1.02x |
| ChaCha20-IETF streaming | 1.32x | 1.01x | 1.14x | 0.99x |
| AES-256-GCM per-call | 0.60x | 0.80x | 1.49x | 2.12x |

---

## 5. Key Findings

### AES-256-GCM with cached handle is the clear winner

AES-256-GCM with a pre-computed key schedule (BCrypt handle reused per connection) is **6.7x faster** than the current XChaCha20-Poly1305 default at the typical game packet size of 1200B. The hardware AES-NI pipeline processes data at 3 GB/s vs 455 MB/s for software ChaCha20.

### Current AES-GCM implementation has a critical bottleneck

The existing `aesgcm_encrypt/decrypt` in `aead_aesgcm.cpp` opens a BCrypt algorithm provider and generates a symmetric key handle **on every call**. This kernel-mode round-trip dominates small packets:

- **64B:** AES-GCM per-call (1,292 ns) is **1.66x slower** than XChaCha20 (776 ns)
- **1200B:** AES-GCM per-call (2,374 ns) is **2.12x faster** than XChaCha20 (5,029 ns)

With cached handles, AES-GCM wins at **every** payload size.

### XChaCha20 streaming offers marginal gains

The streaming API (`crypto_aead_init_x`) avoids the HChaCha20 subkey derivation per packet, saving ~180 ns at 64B (776 → 590 ns). At 1200B the gain is negligible (~2%). Not worth the architectural change.

### ChaCha20-IETF vs XChaCha20 — no performance difference

Despite the smaller nonce (12B vs 24B), IETF ChaCha20-Poly1305 performs identically to XChaCha20 in monocypher. XChaCha20 is strictly better: same speed, safer nonce handling (24 bytes = no birthday-bound concerns).

### CRC32C integrity-only mode is 24x faster than any AEAD

For trusted LAN environments (dedicated server rooms, local play), CRC32C-only mode provides integrity checking at ~213 ns/1200B vs ~5,000 ns for AEAD. This is the `NETUDP_CRC32_ONLY` compile flag.

---

## 6. Recommendation

### Short term: optimize AES-256-GCM implementation

Cache the BCrypt algorithm handle and key handle per connection (inside `KeyEpoch` or alongside it). This is a localized change to `aead_aesgcm.cpp` that would yield:

- **746 ns** encrypt+decrypt @ 1200B (vs 5,029 ns current default)
- **6.7x throughput improvement** for the hot path
- Zero impact on XChaCha20 fallback path

### Default algorithm strategy

| Platform | AES-NI detected | Default |
|----------|:---:|---------|
| Windows / Linux (x86_64) | Yes | AES-256-GCM cached |
| Windows / Linux (x86_64) | No | XChaCha20-Poly1305 |
| ARM / Other | N/A | XChaCha20-Poly1305 |

### Long term: consider AES-GCM intrinsics on Linux

The BCrypt API is Windows-only. For Linux, direct AES-NI + PCLMULQDQ intrinsics (or linking OpenSSL/libcrypto) would provide the same ~750 ns performance. This would make AES-GCM the universal default on any x86_64 with AES-NI.

---

## 7. Industry Comparison

| Library | Algorithm | Notes |
|---------|-----------|-------|
| **netudp (proposed)** | AES-256-GCM cached | 746 ns / 3.0 GB/s |
| netudp (current) | XChaCha20-Poly1305 | 5,029 ns / 455 MB/s |
| Valve GNS | AES-256-GCM | Uses OpenSSL, likely cached handles |
| netcode.io | XChaCha20-Poly1305 | libsodium, similar to our monocypher perf |
| ENet | None | No encryption built-in |

---

## Appendix: Raw JSON

Results saved to `benchmarks/2026-04-13_19-34-25_crypto.json`.
Benchmark source: `bench/bench_crypto.cpp`.
