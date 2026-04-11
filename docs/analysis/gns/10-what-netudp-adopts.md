# 10. What netudp Should Adopt from GNS

## Adopt Directly

| # | Feature | GNS Source | Priority |
|---|---------|-----------|----------|
| 1 | **Ack vector model** (not simple bitmask) | SNP wire format | High |
| 2 | **Multiple frames per packet** | SNP frame encoding | High |
| 3 | **Nagle algorithm** for automatic batching | Configurable per-send | High |
| 4 | **Large message support** (fragmentation + reassembly) | SNP segments | High |
| 5 | **Lanes with priority + weight** | ConfigureConnectionLanes | Medium |
| 6 | **Per-lane statistics** | RealTimeLaneStatus | Medium |
| 7 | **Comprehensive connection statistics** | RealTimeStatus | High |
| 8 | **RTT from ack delay** (no separate ping needed) | Ack frame delay field | High |
| 9 | **Stop-waiting optimization** | Stop waiting frame | Medium |
| 10 | **Batch send API** | SendMessages() | High |
| 11 | **Batch receive API** | ReceiveMessages() returning array | High |
| 12 | **Poll groups** (receive from any connection) | PollGroup | Medium |
| 13 | **Network simulator** built-in | API parameter | Medium |
| 14 | **Message-oriented** (not stream) | Design philosophy | High |
| 15 | **Variable-length encoding** throughout wire format | SNP frames | High |
| 16 | **Send rate estimation** (channel capacity) | m_nSendRateBytesPerSecond | Medium |

## Adopt the Approach, Adapt the Implementation

| Aspect | GNS Approach | netudp Adaptation |
|---|---|---|
| Reliable data model | Byte stream (TCP-like) | **Message-based** (simpler for games) |
| Ack encoding | RLE ack vectors | Simplified ack bitmask (32-64 bits) initially, expandable |
| Lanes | Runtime reconfigurable | Fixed at connection creation (simpler) |
| Per-message reliability | Bitflag per message | **Per-channel** (simpler mental model) |
| Language | C++ with C flat API | Pure C (wider compatibility) |
| Encryption | AES-256-GCM | ChaCha20-Poly1305 default + AES-GCM option |
| Connection auth | Certs + tickets | Connect tokens (netcode.io style) |
