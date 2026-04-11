# 5. Stateless Handshake (DTLS-Inspired)

**File:** `StatelessConnectHandlerComponent.h`

## Design: Server Stores No Per-Client State During Handshake

Partially based on DTLS (Datagram Transport Layer Security) protocol.

## Flow

```
Client                              Server
  │                                    │
  │  1. InitialPacket                  │
  │ ──────────────────────────────────→│  Server generates cookie using HMAC(secret, client_addr)
  │                                    │  Server has 2 rotating secrets (SECRET_COUNT = 2)
  │  2. Challenge                      │
  │     [cookie (20 bytes)]            │
  │ ←──────────────────────────────────│  No per-client state allocated!
  │                                    │
  │  3. ChallengeResponse              │
  │     [HMAC(secret, cookie)]         │
  │ ──────────────────────────────────→│  Validate HMAC, check secret rotation
  │                                    │  NOW allocate connection
  │  4. Ack                            │
  │ ←──────────────────────────────────│  Connection established
  │                                    │
  │  [Control Channel Opens]           │
```

## Handshake Versions (Backward Compatibility)

```cpp
enum class EHandshakeVersion : uint8 {
    Original        = 0,  // Unversioned
    Randomized      = 1,  // Added randomization + versioning
    NetCLVersion    = 2,  // Network CL version for early rejection
    SessionClientId = 3,  // Server session ID + client connection ID (COMPAT BREAK)
    NetCLUpgradeMessage = 4,  // Stateless-level version negotiation
    Latest = NetCLUpgradeMessage
};
```

**Key insight:** UE5 versions the handshake protocol itself, with explicit compatibility breaks. This allows graceful protocol evolution. netudp should include a version field in connect tokens for the same purpose.

## Constants

```cpp
#define SECRET_BYTE_SIZE 64   // Per-secret size
#define SECRET_COUNT 2        // Rotating secrets
#define COOKIE_BYTE_SIZE 20   // Cookie sent to client
```

## Comparison with netcode.io

| Aspect | UE5 StatelessConnect | netcode.io Connect Token |
|---|---|---|
| State before auth | Zero (HMAC cookie) | Zero (encrypted token) |
| Auth mechanism | HMAC secret rotation | Pre-shared key + AEAD token |
| Backend required | No (self-contained) | Yes (generates tokens) |
| Server list | No | Yes (up to 32 servers) |
| User data | No | Yes (256 bytes) |
| Encryption keys | Negotiated after connect | Embedded in token |
| Versioning | EHandshakeVersion enum | Version string in AAD |

**netudp uses netcode.io's approach** (connect tokens with pre-embedded keys) because it supports matchmaking (backend assigns servers) and carries user data. UE5's approach is simpler but doesn't integrate with external auth.
