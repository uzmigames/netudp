# Architecture Overview

**Files:** All in `GameServer/Shared/Networking/` and `GameServer/Server/`

The server follows a 3-layer architecture:

```
┌─────────────────────────────────────────────┐
│ Game Logic (Scene + Entities + PlayerController)
│   1 thread per map, 32 ticks/sec
│   Consumes events from lock-free queue
├─────────────────────────────────────────────┤
│ NetManager (instance per Scene)
│   Event queue (CAS intrusive linked list)
│   ProcessEvents() + Update() called during game tick
├─────────────────────────────────────────────┤
│ I/O Threads (global, shared)
│   Receive Thread: Poll → dispatch → enqueue
│   Send Thread: dequeue → CRC/encrypt → sendto
├─────────────────────────────────────────────┤
│ NanoSockets (C library via P/Invoke)
│   non-blocking UDP socket
└─────────────────────────────────────────────┘
```

**Complete flow of a received packet:**
1. Receive thread calls `UDP.Unsafe.Receive()` (NanoSockets)
2. Reads the packet type byte (`PacketType`)
3. Looks up the `Connection` in `Dictionary<Address, Connection>`
4. Validates CRC32C (or decrypts if ENCRYPT enabled)
5. Enqueues the `ByteBuffer` in the `NetManager` event queue for the connection (CAS lock-free)
6. Game thread calls `Manager.ProcessEvents()` during the tick
7. Drains the queue, dispatches to `Connection.ProcessPacket()`
8. For application data, calls `PacketReceived` delegate → `PlayerController.ReadAllPackets()`
9. Reads the `ServerPacketType`/`ClientPacketType` and dispatches to the specific handler

