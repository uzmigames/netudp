# Complete Analysis — Legacy Server Tales of Shadowland (ToS2/Server1)

Exhaustive analysis of the existing game server networking implementation.

**Platform:** C# / .NET 5-6 (Visual Studio, Docker)  
**Source code:** `K:\Tales Of Shadowland\2022\ToS2\Server1\`  
**Analysis date:** 2026-04-11  
**Original implementation author:** Andre Ferreira (@andrehrferreira)

---

## Index

### Architecture & Transport

1. [Architecture Overview](01-architecture-overview.md) — 3-layer design, complete packet flow
2. [Socket Layer (UDP)](02-socket-layer.md) — NanoSockets, NativeSocket, Address structs
3. [Connection Management](03-connection-management.md) — State machine, storage, timeout, fields
4. [Handshake Protocol](04-handshake-protocol.md) — 4-step ECDH P-521, key derivation
5. [Packet Types (Wire Protocol)](05-packet-types.md) — 10 packet types
6. [Wire Format](06-wire-format.md) — Unreliable, reliable, ACK, encrypted layouts

### Reliability & Delivery

7. [Reliability Engine](07-reliability-engine.md) — Sending, retransmission (30x150ms), ACK
8. [Sequencing and Sliding Window](08-sequencing-and-sliding-window.md) — 16384 slots, bit array, relative comparison
9. [Reliable Ordered Delivery](09-reliable-ordered-delivery.md) — Reorder buffer, 200-packet limit
10. [Unreliable Delivery](10-unreliable-delivery.md) — Fire-and-forget with TickNumber
11. [Packet Batching System](11-packet-batching.md) — Begin/End pattern, multiple msgs per UDP

### Security

12. [Encryption and Key Exchange](12-encryption-and-key-exchange.md) — ECDH P-521, AES-128-GCM, hardware/software
13. [Packet Integrity (CRC32C)](13-packet-integrity-crc32c.md) — SSE4.2, ARM CRC32, slicing-by-16

### Memory & Performance

14. [Buffer and Memory Management](14-buffer-and-memory-management.md) — 2-tier pool, NativeMemory, 3600 bytes/buffer
15. [Ring Buffer (Disruptor Pattern)](15-ring-buffer-disruptor-pattern.md) — SPSC, cache-line padding, fences

### Threading & Game Loop

16. [Threading Model](16-threading-model.md) — Recv + Send + N game threads, CAS queues
17. [Game Loop and Tick System](17-game-loop-and-tick-system.md) — 32 ticks/sec, custom TaskScheduler

### Serialization

18. [Binary Serialization (ByteBuffer)](18-binary-serialization.md) — Direct pointer access, INetSerializable, UTF-8 strings
19. [VarInt and ZigZag Encoding](19-varint-and-zigzag-encoding.md) — LEB128, zigzag, byte savings
20. [Position Quantization and Delta Encoding](20-position-quantization-and-delta-encoding.md) — Float→int, delta, 1-byte rotation
21. [Symbol Table (String Interning)](21-symbol-table-string-interning.md) — Per-connection, ~83% savings

### Game Networking

22. [Ping/Pong and RTT System](22-ping-pong-and-rtt.md) — 5s interval, no smoothing
23. [Rate Limiting and Anti-Flood](23-rate-limiting-and-anti-flood.md) — 1000 pkt/s, exceed = disconnect
24. [Packet Dispatch System](24-packet-dispatch-system.md) — Reflection, 255 handlers, 130+ server types
25. [Area of Interest (AoI)](25-area-of-interest.md) — Spatial grid, 25-unit radius
26. [Reliable vs Unreliable Entities](26-reliable-vs-unreliable-entities.md) — ReliableEntity, AutoDestroyEntity
27. [Entity Tick Acknowledgment](27-entity-tick-ack.md) — Implicit ACK via tick
28. [Custom Per-Connection Headers](28-custom-per-connection-headers.md) — IHeaderWriter interface

### Configuration & Authentication

29. [Protocol Constants](29-protocol-constants.md) — MaxSequence, WindowSize, MTU, ChunkSize
30. [JWT Authentication](30-jwt-authentication.md) — HMAC-SHA256, CharacterClaims

### Conclusions

31. [Identified Gaps](31-identified-gaps.md) — 14 features netudp MUST add
32. [Patterns to Preserve](32-patterns-to-preserve.md) — 15 production-proven patterns

---

## File Map

See [file-map.md](file-map.md) for complete mapping of legacy files → netudp equivalents.
