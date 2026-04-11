# Complete Analysis — netcode.io (Glenn Fiedler / Mas Bandwidth)

Industry-standard reference implementation for secure UDP game client/server connections.

**Platform:** C (single-header library)  
**Repository:** https://github.com/mas-bandwidth/netcode  
**Author:** Glenn Fiedler (Mas Bandwidth LLC)  
**License:** BSD 3-Clause  
**Version:** 1.02  
**Analysis date:** 2026-04-11

---

## What netcode.io IS and IS NOT

**netcode.io IS:**
- A **connection-oriented** protocol over UDP
- Secure client/server connection establishment with connect tokens
- AEAD encryption (XChaCha20-Poly1305 / ChaCha20-Poly1305) for all traffic
- Replay protection
- Challenge/response to prevent IP spoofing

**netcode.io IS NOT:**
- A reliability layer (no ACK, no retransmission, no ordering)
- A channel system (single unreliable channel only)
- A serialization library (raw bytes in, raw bytes out)
- A fragmentation system (1200-byte payload max)
- A compression system

**This is critically important for netudp's design:** netcode.io handles ONLY the connection/security layer. It's designed to be paired with a separate reliability library (like [reliable.io](https://github.com/mas-bandwidth/reliable)). netudp should provide BOTH in one package.

---

## Index

1. [Architecture & Design Philosophy](01-architecture.md)
2. [Connect Token System](02-connect-tokens.md)
3. [Connection Handshake (4-step)](03-handshake.md)
4. [Packet Types & Wire Format](04-packet-format.md)
5. [Encryption (libsodium AEAD)](05-encryption.md)
6. [Replay Protection](06-replay-protection.md)
7. [Client State Machine](07-client-state-machine.md)
8. [Server Connection Management](08-server-management.md)
9. [Public API Design](09-public-api.md)
10. [Implementation Details](10-implementation-details.md)
11. [What netudp Should Adopt](11-what-netudp-adopts.md)
12. [What netudp Does Differently](12-what-netudp-differs.md)
