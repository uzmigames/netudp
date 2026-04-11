# 1. Architecture & Design Philosophy

## Core Design: "Like TCP, but message-oriented, like UDP"

GNS occupies a unique position:
- **Connection-oriented** like TCP (handshake, state machine, graceful close)
- **Message-oriented** like UDP (discrete messages, not byte streams)
- **Reliable + Unreliable** — both in the same connection, on the same socket
- **Ordered within lanes** — not globally ordered across all messages

## Layered Architecture

```
┌───────────────────────────────────────────────────┐
│ Application (ISteamNetworkingSockets API)          │
│   SendMessageToConnection() / ReceiveMessages()   │
├───────────────────────────────────────────────────┤
│ Lanes (priority + weighted fair queueing)          │
│   Multiple logical streams per connection          │
├───────────────────────────────────────────────────┤
│ SNP (Steam Network Protocol)                       │
│   Reliable stream + unreliable datagrams           │
│   Ack vectors (DCCP/QUIC-style)                    │
│   Nagle algorithm for batching                     │
│   Fragmentation/reassembly for large messages      │
├───────────────────────────────────────────────────┤
│ Encryption (AES-256-GCM, Curve25519)               │
├───────────────────────────────────────────────────┤
│ Transport (UDP sockets / Steam Datagram Relay)     │
└───────────────────────────────────────────────────┘
```

## Key Differences from netcode.io

| Aspect | netcode.io | GNS |
|---|---|---|
| Scope | Connection + encryption only | Full transport (connection + reliability + lanes + encryption) |
| Reliability | None | Ack vectors, retransmission, reliable streams |
| Message size | 1200 bytes max | **512 KB** max |
| Channels | 1 (unreliable) | Multiple **lanes** with priority/weight |
| Batching | None | **Nagle algorithm** (configurable) |
| Language | C | C++ (with C flat API) |
| Lines of code | ~8700 | ~30,000+ |
| Complexity | Minimal | Production-grade |

## Production Deployment

GNS powers:
- Counter-Strike 2 (millions of concurrent players)
- Dota 2
- Steam Datagram Relay (SDR) — Valve's global relay network
- Hundreds of third-party games via Steamworks SDK
