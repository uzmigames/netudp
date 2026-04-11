# Wire Format

### 6.1 Unreliable Packet (CRC mode)

```
Offset  Tamanho  Campo
0       1        PacketType (0 = Unreliable)
1       2        Custom header (IHeaderWriter — chunk offsets X,Y)
3       4        TickNumber (uint32)
7       N        Payload (multiplos ServerPacketType/ClientPacketType)
7+N     4        CRC32C
────────────────
Total: 11 + N bytes
```

### 6.2 Reliable Packet (CRC mode)

```
Offset  Tamanho  Campo
0       1        PacketType (7 = Reliable)
1       2        Sequence (int16)
3       4        TickNumber (uint32)
7       N        Payload (multiplos ServerPacketType/ClientPacketType)
7+N     4        CRC32C
────────────────
Total: 11 + N bytes
```

### 6.3 ACK Packet

```
Offset  Tamanho  Campo
0       1        PacketType (3 = Ack)
1       2*K      Sequence numbers (int16 each)
1+2K    4        CRC32C
────────────────
Total: 5 + 2K bytes (K = numero de acks)
```

### 6.4 Encrypted Packet (when ENCRYPT enabled)

```
Offset  Tamanho  Campo
0       1        PacketType (NOT encrypted — associated data)
1       N        Payload encriptado (AES-GCM ciphertext)
1+N     12       Nonce (random per packet)
13+N    16       Tag de autenticacao (AES-GCM)
────────────────
Total: 29 + N bytes
```

**CRITICAL NOTE:** O `#define ENCRYPT` is **commented out** in production. The server ran with CRC32C for integrity only, no real encryption.

