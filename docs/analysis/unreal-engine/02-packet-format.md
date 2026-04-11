# 2. Packet Format & Sequence Tracking

## Wire Format

```
[Packet Header]
  ├── Sequence number (14 bits, wraps at 16384)
  ├── Ack history (variable, up to 256 bits — which of last 256 packets received)
  ├── Packet info (optional):
  │     Jitter clock time (10 bits)
  │     Server frame time (8 bits)
  └── Reserved bits from PacketHandler components

[Bunch 0]
  ├── Channel index (3 bits, 0-7)
  ├── Channel sequence number (per reliable bunch)
  ├── Flags: bOpen, bClose, bReliable, bPartial, bPartialInitial, bPartialFinal
  ├── bHasPackageMapExports, bHasMustBeMappedGUIDs
  └── Bunch payload data

[Bunch 1...]

[Padding to byte boundary]
[Trailing bit (1 bit)]
```

## Key Constants

```cpp
RELIABLE_BUFFER = 512;          // Max outstanding reliable bunches per channel
MAX_PACKETID = 16384;           // 14-bit sequence numbers
MAX_CHSEQUENCE = 1024;          // Per-channel reliable bunch counter
MAX_BUNCH_HEADER_BITS = 256;
MAX_PACKET_HEADER_BITS = 32 + 256 + 1 + 10 + 1 + 8 = ~308 bits
```

## FNetPacketNotify — Sequence Tracking

14-bit packet sequence numbers with 256-entry ack history:

- Sender assigns monotonically increasing 14-bit sequence per packet
- Receiver sends back ack history: bitmask of which of the last 256 packets were received
- Sender determines packet fate: acked (bit set) or nacked (bit clear within window)
- Packets outside window (> 256 behind most recent) assumed lost

## Comparison with Other Approaches

| System | Sequence bits | Ack window | Method |
|---|---|---|---|
| **UE5** | 14 (16384) | 256 packets (bitmask) | Per-packet header bitmask |
| netcode.io | 64 | 256 (uint64 array) | Per-packet check |
| Valve GNS | 64 | RLE ack vectors | SNP frame |
| Server1 | 16 (16384 window) | Explicit ACK packets | Separate ACK messages |

UE5's approach is similar to netcode.io's 256-entry replay window, but uses a bitmask instead of a value array.
