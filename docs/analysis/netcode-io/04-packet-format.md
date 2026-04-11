# 4. Packet Types & Wire Format

## 7 Packet Types

```c
#define NETCODE_CONNECTION_REQUEST_PACKET    0  // Unencrypted, 1078 bytes
#define NETCODE_CONNECTION_DENIED_PACKET     1  // Encrypted, no data
#define NETCODE_CONNECTION_CHALLENGE_PACKET  2  // Encrypted, 308 bytes
#define NETCODE_CONNECTION_RESPONSE_PACKET   3  // Encrypted, 308 bytes
#define NETCODE_CONNECTION_KEEP_ALIVE_PACKET 4  // Encrypted, 8 bytes
#define NETCODE_CONNECTION_PAYLOAD_PACKET    5  // Encrypted, 1-1200 bytes
#define NETCODE_CONNECTION_DISCONNECT_PACKET 6  // Encrypted, no data
```

Only 7 types. Minimalist by design.

## Connection Request (Unencrypted)

```
Offset  Size    Field
0       1       0x00 (prefix byte = zero)
1       13      Version info "NETCODE 1.02\0"
14      8       Protocol ID (uint64)
22      8       Connect token expire timestamp (uint64)
30      24      Connect token nonce
54      1024    Encrypted private connect token data
──────────────
Total: 1078 bytes (fixed)
```

## Encrypted Packets (types 1-6)

### Prefix Byte Encoding

```
Bits 0-3: Packet type (0-6)
Bits 4-7: Number of sequence bytes (1-8)
```

### Variable-Length Sequence Number

Sequence encoded by omitting leading zero bytes:

```
Sequence 0x000003E8 (1000):
  Prefix high bits = 2 (needs 2 bytes)
  Written as: 0xE8, 0x03 (little-endian, reversed)
```

This saves 6 bytes per packet for the first ~65K packets.

### Wire Format

```
[prefix byte]              1 byte
[sequence number]          1-8 bytes (variable)
[encrypted payload]        variable
[HMAC tag]                 16 bytes (Poly1305)
──────────────
Total: 18 + payload bytes minimum
```

### AEAD Associated Data (for all encrypted packets)

```
[version info]    13 bytes    "NETCODE 1.02\0"
[protocol id]     8 bytes     uint64
[prefix byte]     1 byte      prevents packet type modification
──────────────
Total: 22 bytes AAD
```

## Payload Packet

The only packet type carrying application data:

```
[prefix byte]              1 byte     (type=5 | seq_bytes<<4)
[sequence number]          1-8 bytes
[encrypted payload data]   1-1200 bytes
[HMAC]                     16 bytes
```

Max payload: **1200 bytes** (`NETCODE_MAX_PAYLOAD_BYTES`).
Max packet on wire: **1300 bytes** (`NETCODE_MAX_PACKET_BYTES`).
