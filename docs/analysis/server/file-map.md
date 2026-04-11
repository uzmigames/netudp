# Legacy Server File Map

Source: `K:\Tales Of Shadowland\2022\ToS2\Server1\GameServer\`

## Core Networking (`Shared/Networking/`)

| File | Purpose | Lines | netudp Equivalent |
|---|---|---|---|
| `NetManager.cs` | Main server: recv thread, send thread, poll, dispatch, connection lifecycle | ~1160 | `src/core/server.c`, `src/core/client.c` |
| `Connection.cs` | Connection state, reliable/unreliable buffers, sequencing, ACK, ordered delivery | ~494 | `src/connection/connection.c`, `src/reliability/reliability.c` |
| `Connection.DiffieHellman.cs` | ECDH P-521 key exchange, AES key derivation | ~87 | `src/crypto/key_exchange.c` |
| `ByteBuffer.cs` | Packet buffer with unsafe read/write, encryption, serialization, quantization | ~1282 | `src/core/buffer.c`, `src/crypto/aead.c` |
| `ByteBufferPool.cs` | Thread-local + global concurrent buffer pool | ~173 | `src/core/pool.c` |
| `NanoSockets.cs` | P/Invoke wrapper for NanoSockets C library | ~218 | `src/socket/socket_{win,linux,mac}.c` |
| `NativeSocket.cs` | Direct platform socket P/Invoke (ws2_32/libc) | ~273 | `src/socket/socket_{win,linux}.c` |
| `NetUtils.cs` | Address resolution, local IP detection | ~167 | `src/socket/address.c` |
| `RingBuffer.cs` | SPSC ring buffer with cache-line padding | ~273 | `src/core/ringbuffer.c` |
| `FastBitConverter.cs` | Endian-aware byte writing | ~117 | Not needed (C native pointers) |
| `CRC32C.cs` | CRC32C with SSE4.2/ARM/software fallback | ~153 | `src/crypto/crc32c.c` |
| `NetConstants.cs` | Protocol constants (sequence sizes, window, MTU) | ~28 | `include/netudp/config.h` |
| `INetSerializable.cs` | Serialization interface | ~12 | Application-level (not in lib) |
| `ServerPacketType.cs` | Server→Client packet type enum (~130 types) | ~186 | Application-level |
| `ClientPacketType.cs` | Client→Server packet type enum (~40 types) | ~57 | Application-level |
| `ServerPacket.cs` | Base class for server packets | ~21 | Application-level |
| `PacketHandler.cs` | Auto-registering packet dispatch table | ~34 | Application-level |

## Game Server (`Server/`)

| File | Purpose | netudp Relevance |
|---|---|---|
| `Server/Network/PacketHandler.cs` | Packet handler dispatch (reflection-based) | Pattern only — app-level |
| `Server/PacketThrottler.cs` | Per-packet-type rate limiting (commented out) | Inform bandwidth control design |
| `Server/Scene/Scene.cs` | Game world — owns a `NetManager` instance, 32 tick/sec | Threading model reference |
| `Server/Worker.cs` | Server identity (IP + port) | — |
| `Server/ServerController.cs` | Server lifecycle (restart/save) | — |
| `Program.cs` | Entry point — JWT auth, NetManager.Start(), ReliableTimeout config | Handshake flow reference |

## Client (`Client/`)

| File | Purpose |
|---|---|
| `Client/Program.cs` | Minimal test client: `NetManager.Connect()` + `Update()` loop at 60fps |
