# 3. QueueBuffer — Packet Batching over WebSocket

**File:** `src/engine/core/queuebuffer.ts`

## Overview

Application-level packet batching — equivalent to Server1's `BeginReliable/EndReliable` pattern, but over WebSocket instead of UDP.

**This is the most relevant pattern for netudp** from this server.

## Architecture

```
Game Logic → QueueBuffer.addBuffer(socketId, packet)
                    │
                    ▼
              Per-socket queue (ByteBuffer[])
                    │
         ┌──────────┴──────────┐
         │                     │
   Queue size >= 512KB    Timer tick (every deltaTime)
         │                     │
         ▼                     ▼
    combineBuffers()     sendBuffers()
         │
         ▼
    [Queue type byte][pkt1][0xFEFEFEFE][pkt2][0xFEFEFEFE]...[pktN]
         │
         ▼
    socket.send(combinedBuffer)
```

## Key Design

```typescript
export class QueueBuffer {
    private static queues: Map<string, ByteBuffer[]> = new Map();
    private static sockets: Map<string, any> = new Map();
    private static maxBufferSize = 512 * 1024;      // 512KB flush threshold
    private static endOfPacketByte = 0xFE;           // Delimiter byte
    private static endRepeatByte = 4;                // 4x 0xFE = delimiter
}
```

## Batching Logic

1. **addBuffer()** — adds packet to per-socket queue
2. **checkAndSend()** — if queue total >= 512KB, flush immediately
3. **tick()** — periodic timer flushes all non-empty queues

```typescript
// Timer-driven flush (every game tick):
setInterval(() => QueueBuffer.tick(), Maps.deltaTime * 1000);
```

## Combined Buffer Format

```
Offset  Content
0       ServerPacketType.Queue (1 byte — marks this as a batched packet)
1       Packet 1 data
1+N1    0xFE 0xFE 0xFE 0xFE (delimiter)
5+N1    Packet 2 data
...     ...
```

## Duplicate Detection

```typescript
public static isDuplicatePacket(socketId: string, buffer: ByteBuffer): boolean {
    const recentPackets = QueueBuffer.queues.get(socketId) || [];
    const indexBuffer = recentPackets.map((buffer) => buffer.toHex());
    const bufferHex = buffer.toHex();
    return indexBuffer.includes(bufferHex);
}
```

Compares hex representation of entire packet against queued packets. Simple but O(N) per packet.

## Comparison with Server1 Batching

| Aspect | Server1 (UDP) | This (WebSocket) |
|---|---|---|
| Trigger | MTU limit (1200 bytes) | 512KB or timer tick |
| Delimiter | Implicit (each packet has type + sequence) | Explicit `0xFEFEFEFE` sentinel |
| Flush | `Update()` every game tick | `setInterval()` every game tick |
| Overhead | 0 bytes per sub-message | 4 bytes per sub-message (delimiter) |
| Dedup | None | Hex comparison |

## Relevance to netudp

The QueueBuffer pattern reinforces that **packet batching is essential** regardless of transport. Even over TCP/WebSocket (which has no MTU concern), batching reduces syscall overhead and improves throughput. netudp should batch at the MTU level (like Server1) plus support application-level batching APIs.
