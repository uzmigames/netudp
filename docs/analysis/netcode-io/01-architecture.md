# 1. Architecture & Design Philosophy

## Three-Component Architecture

```
┌──────────────┐     HTTPS      ┌──────────────┐
│  Web Backend  │ ◄────────────► │    Client     │
│  (auth, REST) │                │  (game app)   │
└──────┬───────┘                └──────┬───────┘
       │                               │
       │ shared private key            │ connect token (2048 bytes)
       │                               │
┌──────▼───────┐     UDP        ┌──────▼───────┐
│  Dedicated    │ ◄────────────► │    Client     │
│  Server       │   encrypted    │  (netcode)    │
└──────────────┘                └──────────────┘
```

1. **Web Backend** — authenticates clients, generates connect tokens (NOT part of netcode.io)
2. **Dedicated Server** — runs the game, validates connect tokens, manages client slots
3. **Client** — obtains connect token from backend, connects to server via UDP

## Key Design Principles

1. **The server never trusts the client.** All validation happens server-side.
2. **Respond only when necessary.** Minimize server responses to unauthenticated packets.
3. **Ignore malformed packets immediately.** Minimum work for invalid data.
4. **Response must be smaller than request.** Prevents DDoS amplification.
5. **All traffic encrypted after handshake.** No plaintext data packets.
6. **Separate keys per direction.** Client-to-server key != server-to-client key.
7. **Connect tokens are opaque to the client.** The private portion is encrypted.
8. **Single-threaded, poll-based.** No internal threads. Application calls `update()`.

## What It Provides (Scope)

- Connection establishment with cryptographic authentication
- AEAD encryption for all data packets (XChaCha20-Poly1305)
- Replay protection (256-entry sliding window)
- Keep-alive / heartbeat
- Graceful disconnect (redundant disconnect packets)
- Client state machine with detailed error states
- Custom allocator support
- Network simulator for testing
- Loopback connections for local testing
- Packet tagging (DSCP/QoS)

## What It Does NOT Provide

- Reliability (no ACK, no retransmission)
- Ordering (no sequence-based ordering)
- Fragmentation (1200-byte max payload)
- Channels (single unreliable channel)
- Compression
- Bandwidth control
- Batching
- Serialization
