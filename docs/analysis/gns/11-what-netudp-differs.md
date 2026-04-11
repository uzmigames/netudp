# 11. What netudp Does Differently from GNS

## Simpler by Design

GNS is a **production monster** — ~30K lines of C++ with Valve-specific abstractions, P2P/WebRTC/ICE, Steam Datagram Relay integration, cert management, and extensive backward compatibility. netudp targets **simplicity and portability**.

| Aspect | GNS | netudp |
|---|---|---|
| Language | C++ | **C** (wider FFI compatibility) |
| Lines of code | ~30,000+ | Target ~5,000-8,000 |
| Build system | CMake (complex, many options) | **CMake + Zig CC** (simple cross-compile) |
| Encryption | AES-256-GCM (requires AES-NI or OpenSSL) | **ChaCha20-Poly1305** (fast everywhere, vendored) |
| Connection auth | Certs + Steam tickets | **Connect tokens** (netcode.io, simpler) |
| P2P | Full ICE/WebRTC/STUN | **Not in scope** (dedicated servers only) |
| Steam integration | Deep Steamworks SDK integration | **None** (fully independent) |
| Reliable model | Byte stream + message framing | **Message-based** (simpler for game developers) |
| Channel model | Lanes (runtime reconfigurable, per-message reliability) | **Fixed channels** (per-channel reliability, simpler) |
| Nagle | Per-message flag | **Per-channel** setting |
| Memory | C++ new/delete, STL containers | **Pre-allocated pools** (zero-alloc hot path) |

## What netudp Intentionally Omits from GNS

1. **P2P networking** — ICE, STUN, WebRTC, signaling. Not in scope.
2. **Steam Datagram Relay** — Valve's proprietary relay network.
3. **Certificate management** — cert chains, cert stores, signing.
4. **FakeIP** — Steam's IPv4 allocation for P2P.
5. **Identity system** — SteamID, generic identity. netudp uses opaque connection IDs.
6. **Configuration system** — GNS has 100+ configurable options. netudp keeps it minimal.
7. **C++ abstractions** — virtual interfaces, STL, thinker pattern. netudp is pure C.

## Where netudp Adds Value vs GNS

1. **Simpler API** — fewer functions, fewer concepts, faster onboarding
2. **Smaller binary** — 10x fewer lines of code
3. **Easier cross-compilation** — Zig CC builds for any target from any host
4. **No C++ dependency** — works with any language via C FFI
5. **No OpenSSL dependency** — vendored ChaCha20 implementation
6. **Connect token auth** — proven pattern, no cert infrastructure needed
7. **LZ4 compression** — optional, built-in (GNS has none)
8. **CRC32C fast mode** — for LAN/dev scenarios where encryption is overkill
