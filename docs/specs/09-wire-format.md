# Spec 09 — Wire Format & Multi-Frame Packets

## Requirements

### REQ-09.1: Packet Prefix Byte

```
Bits 7-4: Number of sequence bytes (0-8)
Bits 3-0: Packet type (0-15)
```

**Bit 7-4 value mapping:**
| Value | Meaning |
|-------|---------|
| 0     | No sequence (handshake packets: CONNECTION_REQUEST, DENIED) |
| 1-8   | Sequence encoded in 1-8 bytes (variable-length) |
| 9-15  | Reserved. Implementations SHALL reject packets with seq_bytes > 8. |

**Bit 3 (REKEY flag):** For encrypted DATA packets, bit 3 of the packet type nibble serves as the REKEY flag (see spec 04 REQ-04.7). When set, the receiver SHALL derive new keys after processing this packet. Type values 0x04-0x06 remain unaffected (bit 3 = 0 for these types). The REKEY flag is encoded as type `0x0C` (DATA + REKEY).

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
0x05  KEEPALIVE            (empty data, ack fields only)
0x06  DISCONNECT           (graceful close)
0x0C  DATA_REKEY           (DATA + REKEY flag, triggers key rotation)
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
│ Prefix byte (1B)                                │  ← Clear header
│ Sequence (1-8B, variable)                       │     (authenticated
│ Connection ID (4B, uint32 LE)                   │      via AAD)
╞════════════════════════════════════════════════╡
│ ENCRYPTED PAYLOAD                               │
│ ┌────────────────────────────────────────────┐ │
│ │ AckFields (8B):                            │ │
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

**Note:** The `sequence` field is in the **clear header** (not encrypted), and is authenticated indirectly via the AEAD nonce (spec 04 REQ-04.3). The `AckFields` (8 bytes) are the first bytes of the encrypted payload, always present in DATA and KEEPALIVE packets.

AAD = version_info(13) + protocol_id(8) + prefix_byte(1) = 22 bytes.

### REQ-09.4: Frame Types (Inside Encrypted Payload, After AckFields)

Ack information is carried **inline** in the `AckFields` struct (first 8 bytes of encrypted payload), NOT as a separate frame. There is no `ACK_FRAME` — this eliminates the previous duplication between `PacketHeader` ack fields and a separate ack frame.

```
0x02  STOP_WAITING         — offset(1-4B variable)
0x03  UNRELIABLE_DATA      — channel(1) + msg_len(2) + data(N)
0x04  RELIABLE_DATA        — channel(1) + msg_seq(2) + msg_len(2) + data(N)
0x05  FRAGMENT_DATA        — channel(1) + msg_id(2) + frag_idx(1) + frag_cnt(1) + data(N)
0x06  DISCONNECT           — reason(1)
```

**Note on `msg_seq` vs `msg_id`:** `RELIABLE_DATA.msg_seq` and `FRAGMENT_DATA.msg_id` are **independent counters** per channel. A message that is both reliable and fragmented uses `msg_id` from the fragmentation layer (spec 08); the reliability layer tracks it via the packet-level ack mechanism. These counters do not collide because `RELIABLE_DATA` and `FRAGMENT_DATA` are distinct frame types — a message is either small enough to be sent as `RELIABLE_DATA` or large enough to require `FRAGMENT_DATA`.

### REQ-09.5: Multi-Frame Assembly

A single UDP packet SHALL carry multiple frames. The assembler:
1. Writes `AckFields` (8 bytes, always present in DATA and KEEPALIVE packets)
2. Optionally adds STOP_WAITING frame
3. Packs queued messages from channels in priority order
4. Each message becomes UNRELIABLE_DATA, RELIABLE_DATA, or FRAGMENT_DATA frame
5. Stops when remaining space < minimum frame size

### REQ-09.6: Maximum Sizes

```cpp
static constexpr int MTU                    = 1200;  // Safe for most paths
static constexpr int MAX_PACKET_ON_WIRE     = 1400;  // With all headers
static constexpr int CLEAR_HEADER_MAX_SIZE  = 13;    // prefix(1) + seq(8max) + conn_id(4)
static constexpr int ACK_FIELDS_SIZE        = 8;     // ack(2) + ack_bits(4) + ack_delay(2)
static constexpr int AEAD_TAG_SIZE          = 16;    // Poly1305
static constexpr int MAX_PAYLOAD            = MTU - CLEAR_HEADER_MAX_SIZE - ACK_FIELDS_SIZE - AEAD_TAG_SIZE;
// MAX_PAYLOAD ≈ 1163 bytes available for data frames (after ack fields)
```

**Note:** The previous `PACKET_HEADER_SIZE = 15` included a phantom `reserved(2)` field that did not appear in any layout. It has been corrected to `CLEAR_HEADER_MAX_SIZE = 13` (no reserved field). The `ACK_FIELDS_SIZE` is separated because it is inside the encrypted payload, not the clear header.

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
  AckFields + RELIABLE_DATA(ch0, msg1) + RELIABLE_DATA(ch0, msg2) + RELIABLE_DATA(ch0, msg3) + UNRELIABLE_DATA(ch1, msg1)

#### Scenario: Variable-length sequence saves bytes
Given sequence = 42
Then prefix high bits = 1, sequence = 1 byte
And total packet header overhead = 1 + 1 + 4 = 6 bytes (vs 13 if fixed 8-byte seq)

#### Scenario: MTU limit enforced
Given accumulated frame data = 1100 bytes
And next message = 200 bytes
When assembler tries to add it
Then message is deferred to next packet (would exceed MAX_PAYLOAD)
