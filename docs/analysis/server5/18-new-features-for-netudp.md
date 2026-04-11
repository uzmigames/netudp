# 18. New Features for netudp — What Server5 Adds Beyond Server1

## Features to Port Directly to netudp

| # | Feature | Server5 Source | Priority |
|---|---------|---------------|----------|
| 1 | **ChaCha20-Poly1305 AEAD** | `SecureSession.cs`, `AeadChaCha.cs` | High |
| 2 | **X25519 key exchange** | `SecureSession.cs` | High |
| 3 | **HMAC-SHA256 stateless cookies** | `CookieManager.cs` | High |
| 4 | **64-bit replay window** | `SecureSession.cs` | High |
| 5 | **Automatic rekeying** (1GB / 1h) | `SecureSession.cs` | Medium |
| 6 | **LZ4 inline compression** | `LZ4.cs` | Medium |
| 7 | **14-byte structured header** with AAD | `PacketHeader.cs` | High |
| 8 | **3 channel types** (unreliable, reliable ordered, reliable unordered) | `PacketHeader.cs`, `UDPSocket.cs` | High |
| 9 | **Token bucket rate limiting** per IP | `WAFRateLimiter.cs` | High |
| 10 | **Fragment system** | `UDPSocket.cs` | Medium |
| 11 | **Batch I/O helpers** | `UdpBatchIO.cs` | Medium |
| 12 | **RTT-adaptive retransmission** (max 10 retries) | `UDPSocket.cs` | High |
| 13 | **Deterministic nonces** (ConnID + Seq) | `SecureSession.cs` | High |
| 14 | **Separate Tx/Rx keys** | `SecureSession.cs` | High |
| 15 | **HKDF-SHA256 key derivation** with labeled context | `SecureSession.cs` | High |
| 16 | **StructPool with NativeMemory** | `StructPool.cs` | Medium |
| 17 | **FlatBuffer struct (not class)** | `FlatBuffer.cs` | High |
| 18 | **Bit-level I/O** in buffer | `FlatBuffer.cs` | Low |
| 19 | **3D quantization** (FVector/FRotator → short×3) | `FlatBuffer.cs`, `Quantization.cs` | Medium |

## Design Decisions to Adopt

1. **ChaCha20 over AES-GCM as default** — faster on platforms without AES-NI (ARM, older x86), same security level. Keep AES-GCM as compile-time option for AES-NI platforms.

2. **X25519 over P-521** — 32-byte keys vs 130+ bytes, ~10x faster, equally secure for key exchange. No reason to use P-521.

3. **Cookie before state allocation** — Server5's cookie system prevents memory exhaustion from spoofed connections. Critical for public game servers.

4. **Deterministic nonces** — Using ConnectionId + Sequence as nonce eliminates random nonce generation (Server1's approach) and ensures uniqueness mathematically.

5. **Separate Tx/Rx keys** — Prevents reflection attacks where an attacker replays a packet back to its sender.

6. **Structured header as AAD** — The 14-byte header is authenticated but readable without decryption. This allows routing/dispatch without decrypting every packet.

7. **Compress after encrypt** — Server5 applies LZ4 after encryption. While encrypted data doesn't compress well individually, batched game packets with shared structure can still benefit.

## What Server5 Started But Didn't Finish

- Delta sync for entities (packets defined but handler incomplete)
- Entity quantized sync (partially implemented)
- Full game logic (only benchmark packet handler works)
- Integrity system (framework exists, verification incomplete)
- Unreal C++ transpiler (generates code but untested at scale)

## Combined Feature Set for netudp

netudp should implement the **union** of Server1 (proven patterns) + Server5 (modern security):

| Layer | From Server1 | From Server5 | New in netudp |
|---|---|---|---|
| Socket | NanoSockets pattern | Batch I/O | recvmmsg/sendmmsg on Linux |
| Connection | Intrusive linked list, 120s timeout | Cookie anti-spoof, 30s timeout | Challenge token (hybrid) |
| Reliability | Sequence + ACK + retransmit | RTT-adaptive, 10 max retries | ACK bitmask (vs explicit ACKs) |
| Channels | Reliable ordered + unreliable | + Reliable unordered | + Unreliable sequenced |
| Crypto | CRC32C (hw accel) | ChaCha20-Poly1305, X25519, HKDF | Both ChaCha20 + AES-GCM options |
| Compression | — | LZ4 inline | Same, configurable threshold |
| Buffer | ByteBuffer (class) | FlatBuffer (struct) | C buffer with pool |
| Serialization | VarInt, symbol table, quantize | Generic R/W, bit I/O, 3D quantize | All combined |
| Rate limit | 1000 pkt/s disconnect | Token bucket 60/s + burst | Configurable token bucket |
| Replay | — | 64-bit sliding window | 256-bit window |
| Rekey | — | 1GB / 1h auto rekey | Same |
| Fragmentation | — | Basic fragment IDs | Full reassembly with bitmask |
