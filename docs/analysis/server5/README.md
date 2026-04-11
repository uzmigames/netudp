# Complete Analysis — ToS-Server-5 (Unfinished Rewrite)

Exhaustive analysis of the second-generation game server networking implementation.

**Platform:** C# / .NET 8 (modern async, Channels, Span\<T\>)  
**Repository:** https://github.com/andrehrferreira/ToS-Server-5  
**Analysis date:** 2026-04-11  
**Status:** Started but not finished — crypto handshake and entity sync working, game logic incomplete  
**Authors:** Andre Ferreira, Diego Guedes (FlatBuffer)

---

## Key Differences from Server1 (Legacy)

| Feature | Server1 (Legacy) | Server5 (Rewrite) |
|---|---|---|
| Encryption | AES-128-GCM (disabled in prod) | **ChaCha20-Poly1305** (always on, X25519 key exchange) |
| Key exchange | ECDH P-521 (BouncyCastle) | **X25519** (much faster, 32-byte keys) |
| Anti-spoof | None (relied on ECDH cost) | **HMAC-SHA256 cookies** (stateless, 10s TTL) |
| Compression | None | **LZ4** (inline, for packets > 512 bytes) |
| Rate limiting | Packet count (1000/s, disconnect) | **Token bucket** per IP (60/s + 5 burst) |
| Replay protection | None | **64-bit sliding window** |
| Rekeying | None | **Automatic** (1GB or 1 hour threshold) |
| Packet header | 1-byte type only | **14-byte structured header** (ConnID, Channel, Flags, Sequence) |
| Channels | 2 (reliable ordered + unreliable) | **3** (unreliable, reliable ordered, reliable unordered) |
| Fragmentation | None | **Fragment system** (per-connection fragment IDs) |
| Buffer | ByteBuffer (class, GC) | **FlatBuffer** (struct, NativeMemory, IDisposable) |
| Serialization | Custom unsafe | **Generic Write\<T\>/Read\<T\>** + VarInt + bit-level I/O |
| Queues | CAS intrusive linked list | **.NET Channel\<T\>** (bounded/unbounded) |
| Rate limiting | Simple counter | **WAF token bucket** per IP |
| Integrity | None | **Client integrity check** system (anti-tamper) |
| Entity sync | Per-entity packet | **Quantized entity packets** + delta sync |
| Code gen | Manual | **Contract system** with transpiler (C# → Unreal C++) |
| Vectors | 2D (Vector2) | **3D** (FVector X,Y,Z + FRotator Pitch,Yaw,Roll) |
| Batch I/O | None | **UdpBatchIO** helper for bulk send/recv |
| Struct pools | None | **StructPool\<T\>** (NativeMemory, fixed capacity) |
| Connection timeout | 120s | **30s** |
| Reliable retries | 30 × 150ms (4.5s) | **10 retries**, RTT-adaptive interval |

---

## Index

### Core Networking

1. [UDPServer — Main Server](01-udp-server.md) — Connection lifecycle, recv/send threads, packet routing
2. [UDPSocket — Connection State](02-udp-socket.md) — Per-connection state, reliability, fragmentation, channels
3. [Packet Header & Wire Format](03-packet-header-and-wire-format.md) — 14-byte header, flags, channels
4. [Packet Types](04-packet-types.md) — 17 transport types, 5 client game types, 7 server game types

### Security

5. [SecureSession — X25519 + ChaCha20-Poly1305](05-secure-session.md) — Key exchange, AEAD, replay window, rekeying
6. [AeadChaCha — Low-Level Crypto](06-aead-chacha.md) — Seal/Open with ArrayPool, header AAD
7. [CookieManager — Anti-DDoS](07-cookie-manager.md) — HMAC-SHA256 stateless cookies, 10s TTL

### Compression & Integrity

8. [LZ4 Compression](08-lz4-compression.md) — Inline compress/decompress, threshold 512 bytes
9. [Integrity System](09-integrity-system.md) — Client file hash verification, anti-tamper

### Serialization

10. [FlatBuffer — Binary Serialization](10-flat-buffer.md) — Struct-based, generic Read/Write, VarInt, bit I/O
11. [Quantization](11-quantization.md) — Float→short with min/max range, 3D vectors

### Memory & Performance

12. [WAF Rate Limiter](12-waf-rate-limiter.md) — Token bucket per IP, 60 pkt/s + 5 burst
13. [UDP Batch I/O](13-udp-batch-io.md) — Batch send/recv helpers
14. [StructPool & ObjectPool](14-struct-and-object-pool.md) — NativeMemory struct pool, ConcurrentBag object pool
15. [QueueStructLinked](15-queue-struct-linked.md) — Intrusive linked list with node pooling

### Game Layer

16. [Contract System & Transpiler](16-contract-system.md) — Declarative packets, auto-generated C# + Unreal C++
17. [Entity System & Delta Sync](17-entity-system.md) — Quantized sync, delta packets

### Conclusions

18. [New Features for netudp](18-new-features-for-netudp.md) — What Server5 adds beyond Server1

---

## File Map

See parent [file-map.md](../server/file-map.md) for Server1 mapping. Server5 file map:

| Server5 File | Purpose | netudp Equivalent |
|---|---|---|
| `Core/Network/UDPServer.cs` | Main server, connection lifecycle | `src/core/server.c` |
| `Core/Network/UDPSocket.cs` | Per-connection state, channels, reliability | `src/connection/connection.c` |
| `Core/Network/FlatBuffer.cs` | Binary serialization | `src/core/buffer.c` |
| `Core/Network/PacketHeader.cs` | 14-byte structured header | `src/core/packet.h` |
| `Core/Network/Security/SecureSession.cs` | X25519 + ChaCha20 session | `src/crypto/session.c` |
| `Core/Network/Security/AeadChaCha.cs` | AEAD encrypt/decrypt | `src/crypto/chacha20poly1305.c` |
| `Core/Network/Security/CookieManager.cs` | Anti-spoof cookies | `src/connection/cookie.c` |
| `Core/Network/WAFRateLimiter.cs` | Token bucket rate limit | `src/connection/ratelimit.c` |
| `Core/Network/UdpBatchIO.cs` | Batch send/recv | `src/socket/batch_io.c` |
| `Core/Utils/LZ4.cs` | LZ4 compression | `src/compress/lz4.c` |
| `Core/Utils/CRC32C.cs` | CRC32C checksum | `src/crypto/crc32c.c` |
| `Core/Utils/Quantization.cs` | Float quantization | `src/core/quantize.c` |
| `Core/Utils/StructPool.cs` | NativeMemory struct pool | `src/core/pool.c` |
| `Core/Utils/QueueStructLinked.cs` | Intrusive linked list | `src/core/queue.c` |
