# 7. Encryption & Key Exchange

## Algorithms

- **Symmetric encryption:** AES-256-GCM (hardware accelerated via AES-NI)
- **Key exchange:** Curve25519 (X25519 ECDH)
- **Signatures:** Ed25519 (for certificates)
- **Key derivation:** QUIC-style design

## Certificate-Based Auth (vs netcode.io's Token-Based)

GNS uses a certificate chain system — more complex but supports P2P where there's no central backend to issue connect tokens. For dedicated servers, this is overkill.

## AES-256-GCM vs ChaCha20-Poly1305

GNS chose AES-GCM because Valve targets hardware with AES-NI (modern x86). On platforms without AES-NI (older ARM, some mobile), ChaCha20 is faster. netudp defaults to ChaCha20 for portability, with AES-GCM as compile-time option.
