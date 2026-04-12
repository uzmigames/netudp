# netudp — Technical Specifications

Detailed specs for every module. Each spec contains: requirements (SHALL/MUST), data structures, API signatures, constants, and Given/When/Then scenarios.

## Index

| # | Spec | Scope |
|---|---|---|
| [00](00-project-setup.md) | Project Setup | CMake, CI, headers, directory structure, static analysis |
| [01](01-simd.md) | SIMD Detection & Dispatch | Runtime detection, dispatch table, per-ISA files, non-temporal stores |
| [02](02-platform-sockets.md) | Platform Sockets | Socket abstraction, Address type, send/recv/batch, IPv4/IPv6 |
| [03](03-memory-pools.md) | Zero-GC Memory & Pools | Allocator, Pool\<T\>, FixedRingBuffer, FixedHashMap, zero-GC verification |
| [04](04-crypto.md) | Cryptography | ChaCha20-Poly1305, XChaCha20, CRC32C, nonce, keys, replay, rekeying |
| [05](05-connect-tokens.md) | Connect Tokens & Handshake | Token format (2048B), 4-step handshake, client state machine, rate limit, DDoS |
| [06](06-channels.md) | Channel System | 4 types, priority+weight, Nagle, per-channel compression, stats |
| [07](07-reliability.md) | Dual-Layer Reliability | Packet ack+bits, per-channel message seq, RTT, retransmit, stop-waiting |
| [08](08-fragmentation.md) | Fragmentation & Reassembly | Fragment header, bitmask, fragment-level retransmit, timeout |
| [09](09-wire-format.md) | Wire Format & Multi-Frame | Prefix byte, variable seq, frame types, multi-frame assembly, MTU |
| [10](10-bandwidth-congestion.md) | Bandwidth & Congestion | Token bucket, QueuedBits, AIMD, DDoS escalation |
| [11](11-compression.md) | Compression (netc) | Integration, stateful/stateless, passthrough, pipeline position |
| [12](12-statistics.md) | Statistics & Diagnostics | Per-connection, per-channel, global server stats |
| [13](13-public-api.md) | Public API (extern "C") | Lifecycle, send/recv, flags, handlers, callbacks, buffer, token gen |
| [14](14-benchmarks.md) | Benchmark Suite | 12 benchmarks, targets, CI regression, SIMD comparison rule |
| [15](15-network-simulator.md) | Network Simulator | Latency, jitter, loss, duplicate, reorder, incoming lag |

### SDK & Engine Integration Specs

| # | Spec | Scope |
|---|---|---|
| [16](16-sdk-cpp-wrapper.md) | C++ Wrapper (sdk/cpp) | RAII Server/Client/Message, BufferWriter fluent API, BufferReader, ConnectToken |
| [17](17-sdk-uzengine.md) | UzEngine Integration | NetworkingSubsystem (ISubsystem), EventQueue, PoolAllocator, ECS components |
| [18](18-sdk-unreal.md) | Unreal Engine 5 Plugin | UGameInstanceSubsystem, Blueprint API, dynamic delegates, platform libs |
| [19](19-sdk-unity.md) | Unity C# Bindings | P/Invoke, NativeArray zero-GC pattern, NetudpServer/Client wrappers |
| [20](20-sdk-godot.md) | Godot 4 GDExtension | GDScript API, signals, PackedByteArray, connect token generation |
