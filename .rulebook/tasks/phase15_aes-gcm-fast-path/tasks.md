## 1. Implementation

- [ ] 1.1 Add AES-NI detection via CPUID in `src/core/platform.h`
- [ ] 1.2 Add `NETUDP_HAVE_AES_NI` compile flag and runtime detection function
- [ ] 1.3 Implement `aead_encrypt_aesni()` using AES-256-GCM intrinsics (`_mm_aesenc_si128`)
- [ ] 1.4 Implement `aead_decrypt_aesni()` with constant-time MAC verification
- [ ] 1.5 Add function pointer dispatch: select AES-GCM or XChaCha20 at init time
- [ ] 1.6 Add `crypto_mode` field to `netudp_server_config_t` (XCHACHA20 default, AES_GCM opt-in)
- [ ] 1.7 Ensure XChaCha20 fallback on CPUs without AES-NI (ARM, old x86)
- [ ] 1.8 Benchmark: XChaCha20 vs AES-GCM per-packet cost
- [ ] 1.9 Build and verify on MSVC, GCC, Clang (AES-NI availability varies)

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
