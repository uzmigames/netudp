# 6. Encryption & DDoS Protection

## Encryption
- `FEncryptionComponent` in PacketHandler pipeline
- AES-256-GCM (authenticated encryption with associated data)
- Key exchange happens at game level (not in handshake)
- `EnableEncryption()` / `EnableEncryptionServer()` after connection established
- DTLS handler available as alternative (`DTLSHandlerComponent`)

## DDoS Detection (`FDDoSDetection`)
- Tracks per-IP packet rates with escalating severity states
- Counters: NonConnPackets, NetConnPackets, DisconnPackets, BadPackets, ErrorPackets
- Each severity level has: EscalateQuotaPacketsPerSec, PacketLimitPerFrame, PacketTimeLimitMSPerFrame
- Auto-cooldown after attack subsides
- Analytics delegate for monitoring

## netudp comparison
- netudp uses ChaCha20-Poly1305 (faster without AES-NI, vendored)
- netudp uses token bucket per IP (simpler than UE5's multi-counter escalation, sufficient for dedicated servers)
