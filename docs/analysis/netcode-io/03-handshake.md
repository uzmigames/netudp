# 3. Connection Handshake (4-step)

## Flow

```
Client                                Server
  │                                      │
  │ 1. CONNECTION_REQUEST                │
  │    [0x00][version][proto_id]         │
  │    [expire][nonce]                   │
  │    [encrypted_private_token(1024)]   │
  │ ────────────────────────────────────→│
  │                                      │ Decrypt private token with private key
  │                                      │ Validate: version, proto_id, expire,
  │                                      │   server address in token, no duplicate
  │                                      │ Add encryption mapping for client addr
  │                                      │
  │ 2. CONNECTION_CHALLENGE              │
  │    [prefix][seq]                     │
  │    [challenge_seq(8)]                │
  │    [encrypted_challenge_token(300)]  │
  │ ←────────────────────────────────────│
  │                                      │
  │ Client stores challenge token        │
  │                                      │
  │ 3. CONNECTION_RESPONSE               │
  │    [prefix][seq]                     │
  │    [challenge_seq(8)]                │
  │    [encrypted_challenge_token(300)]  │
  │ ────────────────────────────────────→│
  │                                      │ Decrypt challenge token
  │                                      │ Verify client_id not duplicate
  │                                      │ Assign to client slot
  │                                      │
  │ 4. CONNECTION_KEEP_ALIVE             │
  │    [prefix][seq]                     │
  │    [client_index(4)][max_clients(4)] │
  │ ←────────────────────────────────────│
  │                                      │
  │ Client now CONNECTED                 │
  │ All subsequent packets encrypted     │
```

## Anti-Spoof: Challenge Token

The challenge token proves the client can **receive** packets at its claimed address:

1. Server sends encrypted challenge token to client's address
2. Client echoes it back (can only do so if it received it)
3. Spoofed-IP clients never receive the challenge → can't respond

Challenge token encrypted with **server-generated random key** (not the connect token key). Nonce is an incrementing sequence. Contains client_id + user_data.

## Anti-DDoS Properties

- **Step 1**: 1078-byte request → server does decrypt + validation. If invalid → ignore (no response, no state)
- **Step 2**: 344-byte challenge → smaller than request (no amplification)
- **Connect token reuse**: server tracks HMAC of used tokens. Same token can't connect twice from different IPs
- **Server full**: small DENIED packet (smaller than request)

## Comparison with Our Implementations

| Aspect | Server1 (ECDH) | Server5 (Cookie) | netcode.io |
|---|---|---|---|
| Auth mechanism | ECDH P-521 key exchange | HMAC cookie + X25519 | Connect token (pre-shared key) |
| Backend required | No | No | **Yes** (generates tokens) |
| Server state before auth | DH connection entry | None (stateless cookie) | Encryption mapping only |
| Anti-spoof | ECDH (implicit) | Cookie | **Challenge token** |
| Key exchange | Online (DH) | Online (X25519) | **Offline** (keys in token) |
| Crypto cost | P-521 (expensive) | X25519 (fast) | Only symmetric (cheapest) |
