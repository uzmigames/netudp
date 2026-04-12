## 1. Vendored Crypto (P2-A) — spec 04
- [ ] 1.1 Vendor monocypher library (or minimal subset): copy monocypher.c/.h into src/crypto/vendor/, add to CMakeLists.txt as OBJECT library
- [ ] 1.2 Create src/crypto/aead.h/.cpp: aead_encrypt/aead_decrypt wrapping ChaCha20-Poly1305, key[32], nonce[12], AAD, plaintext→ciphertext+16 tag
- [ ] 1.3 Create src/crypto/xchacha.h/.cpp: xchacha_encrypt/xchacha_decrypt wrapping XChaCha20-Poly1305, key[32], nonce[24], for connect token encryption
- [ ] 1.4 Create src/crypto/crc32c.h/.cpp: crc32c() function using g_simd->crc32c dispatch (generic table already in Phase 1, wire up SSE4.2 _mm_crc32_u64 and ARM __crc32cd)
- [ ] 1.5 Create src/crypto/random.h/.cpp: random_bytes() — CryptGenRandom (Windows), /dev/urandom (Linux), arc4random_buf (macOS)
- [ ] 1.6 Tests: test_aead.cpp — encrypt/decrypt round-trip, tampered ciphertext returns -1, known test vectors from RFC 8439

## 2. Connect Token (P2-B) — spec 05
- [ ] 2.1 Create src/connection/connect_token.h: ConnectToken struct matching spec 05 REQ-05.1 (2048 bytes), PrivateConnectToken struct (1024 bytes pre-encryption)
- [ ] 2.2 Implement netudp_generate_connect_token() in src/api.cpp: validate params (1-32 servers, private data fits 1024B), serialize public/private portions, random nonce, XChaCha20 encrypt private data, write to output buffer
- [ ] 2.3 Implement token validation (server-side): decrypt private data with XChaCha20, verify server address in token, check expiry, extract keys + user_data + client_id
- [ ] 2.4 Implement token fingerprint tracking: HMAC-SHA256(private_key, encrypted_private_data) truncated to 8 bytes, FixedHashMap<TokenFingerprint, {ip, expire_time}> cache, reject same fingerprint from different IP
- [ ] 2.5 Tests: test_connect_token.cpp — generate token, validate succeeds, expired token rejected, wrong server address rejected, tampered token decryption fails, fingerprint reuse from different IP rejected

## 3. Replay Protection (P2-C) — spec 04 REQ-04.9
- [ ] 3.1 Create src/crypto/replay.h/.cpp: ReplayProtection struct (most_recent uint64, received[256] uint64), check(nonce) returns true if duplicate/too-old, advance(nonce) marks received, reset() clears window
- [ ] 3.2 Tests: test_replay.cpp — sequential nonces pass, duplicate rejected, old nonce (>256 behind) rejected, out-of-order within window accepted

## 4. AEAD Packet Encryption (P2-D) — spec 04
- [ ] 4.1 Create src/crypto/packet_crypto.h/.cpp: KeyEpoch struct (tx_key[32], rx_key[32], tx_nonce_counter uint64, rx_nonce_counter uint64, bytes_transmitted, epoch_start_time, epoch_number), build_nonce(counter, out[12])
- [ ] 4.2 Implement packet_encrypt(): build nonce from tx_nonce_counter, construct AAD (version_info 13B + protocol_id 8B + prefix_byte 1B = 22B), aead_encrypt, increment tx_nonce_counter and bytes_transmitted
- [ ] 4.3 Implement packet_decrypt(): extract nonce_counter, replay_check first, build nonce, construct AAD, aead_decrypt, if OK call replay_advance
- [ ] 4.4 Tests: test_packet_crypto.cpp — encrypt/decrypt round-trip, replay detection, AAD mismatch rejection, nonce counter increments correctly

## 5. Handshake + Client State Machine (P2-E) — spec 05
- [ ] 5.1 Create src/connection/rate_limiter.h/.cpp: TokenBucket struct (rate=60, burst=10, tokens starts at BURST, refill with min(tokens+rate*dt, BURST)), FixedHashMap<AddressHash, TokenBucket, 4096> per-IP map, entries expire after 30s
- [ ] 5.2 Create src/connection/client_state.h: ClientState enum (-6 to 3: TOKEN_EXPIRED, INVALID_TOKEN, CONNECTION_TIMED_OUT, RESPONSE_TIMED_OUT, REQUEST_TIMED_OUT, CONNECTION_DENIED, DISCONNECTED, SENDING_REQUEST, SENDING_RESPONSE, CONNECTED)
- [ ] 5.3 Implement CONNECTION_REQUEST handling (server): verify packet size == 1078, check version/protocol_id/expiry, decrypt private token, validate server address in token, check client address/id not already connected, check token fingerprint, rate limit check — on pass: generate challenge token with random server key, send CONNECTION_CHALLENGE
- [ ] 5.4 Implement CONNECTION_CHALLENGE generation + validation: server generates challenge_token = encrypt(challenge_seq + client_id + user_data, server_challenge_key), send [prefix][seq][challenge_seq][encrypted_challenge(300B)]; client receives and stores
- [ ] 5.5 Implement CONNECTION_RESPONSE + establishment: client echoes challenge, server decrypts challenge_token, verifies client_id matches, assigns connection slot (with generation counter), creates KeyEpoch from token keys, sends CONNECTION_KEEPALIVE with client_index + max_clients; client transitions to CONNECTED
- [ ] 5.6 Implement multi-server fallback (client): on timeout in SENDING_REQUEST/SENDING_RESPONSE states, try next server address from token, cycle through all servers, transition to error state if none respond
- [ ] 5.7 Implement connection timeout: configurable timeout_seconds (from token), disconnect if no keepalive received within timeout period, graceful disconnect with redundant DISCONNECT packets (3x)
- [ ] 5.8 Create src/server.cpp: netudp_server_create/start/stop/update/destroy lifecycle, connection slots array (max_clients), per-slot KeyEpoch + ReplayProtection + state, update() drives recv→process→callbacks
- [ ] 5.9 Create src/client.cpp: netudp_client_create/connect/update/disconnect/destroy lifecycle, single connection state machine, update() drives send/recv based on current state
- [ ] 5.10 Integration test: full connection flow — generate token → client connect → handshake completes → encrypted send/recv → disconnect

## 6. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 6.1 Update CHANGELOG.md: "Added: ChaCha20-Poly1305 AEAD, connect tokens, 4-step handshake, replay protection, per-IP rate limiting, client state machine"
- [ ] 6.2 All tests pass: test_aead, test_connect_token, test_replay, test_packet_crypto, test_handshake_integration (full flow)
- [ ] 6.3 Run clang-tidy — zero warnings on all new src/crypto/ and src/connection/ files
