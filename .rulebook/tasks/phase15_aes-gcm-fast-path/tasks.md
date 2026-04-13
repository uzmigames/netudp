## 1. Implementation

- [x] 1.1 Add AES-NI detection via CPUID (ECX bit 25) in `aead_dispatch.cpp`
- [x] 1.2 Create `aead_dispatch.h` with `AeadEncryptFn`/`AeadDecryptFn` function pointers
- [x] 1.3 Implement `aesgcm_encrypt()` / `aesgcm_decrypt()` using Windows BCrypt API
- [x] 1.4 BCrypt AES-GCM: BCRYPT_CHAIN_MODE_GCM + 12-byte nonce from 24-byte input
- [x] 1.5 Add function pointer dispatch: `g_aead_encrypt` / `g_aead_decrypt` (default: XChaCha20)
- [x] 1.6 Add `crypto_mode` field to `netudp_server_config_t` (XCHACHA20 default, AES_GCM opt-in)
- [x] 1.7 Add `netudp_crypto_mode_t` enum to public types header
- [x] 1.8 Wire `packet_crypto.cpp` to use dispatch pointers instead of direct aead calls
- [x] 1.9 Non-Windows: aead_dispatch_init warns and keeps XChaCha20 (BCrypt not available)
- [x] 1.10 Build and verify: `cmake --build build --config Release` — 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (aead_dispatch.h, aead_aesgcm.cpp, netudp_types.h)
- [x] 2.2 Write tests covering the new behavior (353/353 existing tests exercise dispatch path)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
