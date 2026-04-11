# Identified Gaps

What the legacy server did **NOT** implement and that netudp **MUST** have:

| # | Feature | Impact | Priority |
|---|---------|---------|------------|
| 31.1 | **Fragmentacao de mensagens** | Messages > 1200 bytes impossible | High |
| 31.2 | **MTU Discovery** | Hardcoded 1200, loses efficiency on good networks | Medium |
| 31.3 | **Reliable Unordered** | Only had reliable ordered — everything serialized | High |
| 31.4 | **Unreliable Sequenced** | No dropping of stale packets | Medium |
| 31.5 | **Retransmissao adaptativa (RTT)** | Fixed 150ms timeout — inefficient at high/low latency | High |
| 31.6 | **Controle de congestionamento** | Only packet count, no backpressure | High |
| 31.7 | **Challenge token anti-spoof** | ECDH P-521 is too expensive for handshake | High |
| 31.8 | **Replay protection** | CRC32C does not prevent replay | Medium |
| 31.9 | **API de estatisticas** | No connection quality metrics | Medium |
| 31.10 | **Desconexao confirmada** | Fire-and-forget, no retry | Low |
| 31.11 | **Migracao de IP** | IP change = disconnect | Low |
| 31.12 | **Criptografia habilitada** | Code existed but was disabled | High |
| 31.13 | **Bandwidth control por conexao** | No bytes/sec limit, only packets/sec | Medium |
| 31.14 | **Connection limit** | ConnectionId is byte (0-255) | High |

