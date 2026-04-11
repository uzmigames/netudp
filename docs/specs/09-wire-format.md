# Spec 09 — Wire Format & Multi-Frame Packets

## Requirements

### REQ-09.1: Packet Prefix Byte

```
Bits 7-4: Number of sequence bytes (1-8)
Bits 3-0: Packet type (0-15)
```

Unencrypted packet types (handshake):
```
0x00  CONNECTION_REQUEST    (always prefix=0x00, seq_bytes=0)
0x01  CONNECTION_DENIED
0x02  CONNECTION_CHALLENGE
0x03  CONNECTION_RESPONSE
```

Encrypted packet types:
```
0x04  DATA                 (normal traffic — contains frames)
0x05  KEEPALIVE            (empty data, ack header only)
0x06  DISCONNECT           (graceful close)
```

### REQ-09.2: Variable-Length Sequence

Sequence number encoded in 1-8 bytes (high zero bytes omitted):

```
Sequence 42 (0x2A):          1 byte  → prefix high=1
Sequence 1000 (0x03E8):      2 bytes → prefix high=2
Sequence 70000 (0x011170):   3 bytes → prefix high=3
Sequence 0xFFFFFFFFFFFFFFFF: 8 bytes → prefix high=8
```

Written little-endian, one byte at a time.

### REQ-09.3: Encrypted Packet Layout

```
┌────────────────────────────────────────────────┐
│ Prefix byte (1B)                                │  ← Authenticated (AAD)
│ Sequence (1-8B, variable)                       │
│ Connection ID (4B, uint32 LE)                   │
╞════════════════════════════════════════════════╡
│ ENCRYPTED PAYLOAD                               │
│ ┌────────────────────────────────────────────┐ │
│ │ PacketHeader (10B):                        │ │
│ │   ack(2) + ack_bits(4) + ack_delay(2)     │ │
│ ├────────────────────────────────────────────┤ │
│ │ Frame 0: type(1B) + data(variable)        │ │
│ ├────────────────────────────────────────────┤ │
│ │ Frame 1: type(1B) + data(variable)        │ │
│ ├────────────────────────────────────────────┤ │
│ │ ...more frames...                          │ │
│ └────────────────────────────────────────────┘ │
╞════════════════════════════════════════════════╡
│ AEAD Tag (16B, Poly1305)                        │
└────────────────────────────────────────────────┘
```

AAD = version_info(13) + protocol_id(8) + prefix_byte(1) = 22 bytes.

### REQ-09.4: Frame Types (Inside Encrypted Payload)

```
0x01  ACK_FRAME            — ack(2) + ack_bits(4) + ack_delay(2) = 8B
0x02  STOP_WAITING         — offset(1-4B variable)
0x03  UNRELIABLE_DATA      — channel(1) + msg_len(2) + data(N)
0x04  RELIABLE_DATA        — channel(1) + msg_seq(2) + msg_len(2) + data(N)
0x05  FRAGMENT_DATA        — channel(1) + msg_id(2) + frag_idx(1) + frag_cnt(1) + data(N)
0x06  DISCONNECT           — reason(1)
```

### REQ-09.5: Multi-Frame Assembly

A single UDP packet SHALL carry multiple frames. The assembler:
1. Starts with ACK frame (always present in data packets)
2. Optionally adds STOP_WAITING frame
3. Packs queued messages from channels in priority order
4. Each message becomes UNRELIABLE_DATA, RELIABLE_DATA, or FRAGMENT_DATA frame
5. Stops when remaining space < minimum frame size

### REQ-09.6: Maximum Sizes

```cpp
static constexpr int MTU                    = 1200;  // Safe for most paths
static constexpr int MAX_PACKET_ON_WIRE     = 1400;  // With all headers
static constexpr int PACKET_HEADER_SIZE     = 15;    // prefix(1) + seq(8max) + conn_id(4) + reserved(2)
static constexpr int AEAD_TAG_SIZE          = 16;    // Poly1305
static constexpr int MAX_PAYLOAD            = MTU - PACKET_HEADER_SIZE - AEAD_TAG_SIZE;
// MAX_PAYLOAD ≈ 1169 bytes available for frames
```

### REQ-09.7: CONNECTION_REQUEST Format (Unencrypted, Fixed 1078 Bytes)

```
Offset  Size    Field
0       1       0x00 (prefix: type=0, seq_bytes=0)
1       13      version_info
14      8       protocol_id
22      8       expire_timestamp
30      24      connect_token_nonce
54      1024    encrypted_private_connect_token
────────────────
Total: 1078 bytes (fixed, no padding)
```

## Scenarios

#### Scenario: Multi-frame packet
Given channel 0 has 3 small reliable messages (20B each)
And channel 1 has 1 unreliable message (100B)
When flush is triggered
Then one UDP packet contains:
  ACK frame + RELIABLE_DATA(ch0, msg1) + RELIABLE_DATA(ch0, msg2) + RELIABLE_DATA(ch0, msg3) + UNRELIABLE_DATA(ch1, msg1)

#### Scenario: Variable-length sequence saves bytes
Given sequence = 42
Then prefix high bits = 1, sequence = 1 byte
And total packet header overhead = 1 + 1 + 4 = 6 bytes (vs 13 if fixed 8-byte seq)

#### Scenario: MTU limit enforced
Given accumulated frame data = 1100 bytes
And next message = 200 bytes
When assembler tries to add it
Then message is deferred to next packet (would exceed MAX_PAYLOAD)
