# Complete Analysis — tos-mmorpg-server (TypeScript/NestJS WebSocket)

Third implementation of Tales of Shadowland — TypeScript with NestJS and WebSocket transport.

**Platform:** TypeScript / NestJS / Node.js  
**Transport:** WebSocket (uWebSockets.js + ws fallback)  
**Repository:** https://github.com/andrehrferreira/tos-mmorpg-server  
**Analysis date:** 2026-04-11  
**Author:** Andre Ferreira

---

## Key Difference: WebSocket instead of UDP

This server uses **WebSocket over TCP** rather than raw UDP. This fundamentally changes the transport characteristics:

| Aspect | UDP (Server1/Server5) | WebSocket (this server) |
|---|---|---|
| Transport | Raw UDP datagrams | TCP + WebSocket framing |
| Reliability | Custom (ACK/retransmit) | TCP handles it |
| Ordering | Custom (sequence numbers) | TCP guarantees it |
| Fragmentation | Custom or none | TCP handles it |
| Head-of-line blocking | No (UDP advantage) | Yes (TCP drawback) |
| Connection handshake | Custom (cookie/ECDH) | TCP + WS upgrade + app auth |
| Browser support | No (needs native client) | Yes (browser WebSocket API) |
| Server framework | Custom event loop | NestJS + uWebSockets.js |

---

## Index

1. [Transport Layer — uWebSockets.js Adapter](01-transport-layer.md)
2. [ByteBuffer — TypeScript Implementation](02-bytebuffer.md)
3. [QueueBuffer — Packet Batching over WebSocket](03-queuebuffer.md)
4. [Packet System](04-packet-system.md)
5. [Encryption — XOR Cipher](05-encryption.md)
6. [Packet Types](06-packet-types.md)
7. [Relevance to netudp](07-relevance-to-netudp.md)
