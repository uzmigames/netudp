# Complete Analysis — Valve GameNetworkingSockets (GNS)

Valve's open-source production networking library, powering CS2, Dota 2, and the Steam Datagram Relay.

**Platform:** C++ (with C flat API)  
**Repository:** https://github.com/ValveSoftware/GameNetworkingSockets  
**Author:** Valve Corporation  
**License:** BSD 3-Clause  
**Analysis date:** 2026-04-11

---

## What GNS IS

The most feature-complete open-source game networking library in existence. It provides:

- Connection-oriented, **message-oriented** (not stream-oriented) API
- Reliable AND unreliable message types
- **Messages can be larger than MTU** — built-in fragmentation/reassembly + retransmission
- Sophisticated reliability layer based on **ack vectors** (DCCP RFC 4340 / QUIC-style)
- **AES-256-GCM** encryption, Curve25519 key exchange
- **Multiple message lanes** with priority and weighted fair queueing
- **Nagle algorithm** for automatic batching
- Network simulation (latency, loss, reordering, duplicate)
- Detailed real-time statistics (ping, quality, throughput, queue depth)
- IPv4 and IPv6
- P2P via ICE/WebRTC NAT traversal
- Cross-platform (Windows, Linux, Mac, consoles, mobile)
- **512 KB max message size** (vs netcode.io's 1200 bytes)

What it does NOT provide:
- Entity serialization / delta encoding
- Compression

---

## Index

1. [Architecture & Design Philosophy](01-architecture.md)
2. [Public API (ISteamNetworkingSockets)](02-public-api.md)
3. [Message Model & Send Flags](03-message-model.md)
4. [Lanes — Multi-Stream Prioritization](04-lanes.md)
5. [SNP Wire Format — The Reliability Protocol](05-snp-wire-format.md)
6. [Ack Vector System (DCCP/QUIC-Style)](06-ack-vectors.md)
7. [Encryption & Key Exchange](07-encryption.md)
8. [Connection Statistics](08-statistics.md)
9. [Nagle Algorithm & Batching](09-nagle-batching.md)
10. [What netudp Should Adopt from GNS](10-what-netudp-adopts.md)
11. [What netudp Does Differently](11-what-netudp-differs.md)
