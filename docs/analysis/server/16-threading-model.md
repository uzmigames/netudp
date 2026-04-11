# Threading Model

**File:** `NetManager.cs`

### 16.1 Thread Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ RECEIVE THREAD (1 global)                                       │
│ - UDP.Poll(100ms) + drain ate 1000 pacotes                     │
│ - DH key exchange, connect requests, ping/pong                 │
│ - CRC32C check / decrypt                                       │
│ - Enqueue para o NetManager correto (lock-free CAS)            │
│ - Ping a cada 5s, cleanup DH timeouts                          │
│ - ConcurrentByteBufferPool.Merge() periodico                   │
├─────────────────────────────────────────────────────────────────┤
│ SEND THREAD (1 global)                                          │
│ - WaitOne(ReliableTimeout) no AutoResetEvent                   │
│ - Retransmissao de ReliableList                                │
│ - Drena GlobalSendQueue (lock)                                 │
│ - Append CRC32C / encrypt                                      │
│ - UDP.Unsafe.Send()                                            │
│ - ConcurrentByteBufferPool.Merge()                             │
├─────────────────────────────────────────────────────────────────┤
│ GAME THREAD #1 (Scene "Forest")                                 │
│ - 32 ticks/sec (DeltaTime = 1/32)                              │
│ - TaskQueue drain → Tick():                                     │
│   → Manager.ProcessEvents() (drain event queue)                │
│   → TimerQueue.Update()                                        │
│   → Updateables loop (entities, AI)                            │
│   → PlayerControllers loop (AoI, packets)                      │
│   → Manager.Update(delta) (flush buffers, check timeouts)      │
│   → ConcurrentByteBufferPool.Merge()                          │
├─────────────────────────────────────────────────────────────────┤
│ GAME THREAD #2 (Scene "Desert")   ...   GAME THREAD #N         │
└─────────────────────────────────────────────────────────────────┘
```

### 16.2 Lock-Free Communication (CAS)

```csharp
// Enqueue de evento (receive thread → game thread)
private void EnqueueEvent(ByteBuffer buffer) {
    while (true) {
        ByteBuffer temp = EventQueue;
        buffer.Next = temp;
        if (Interlocked.CompareExchange(ref EventQueue, buffer, temp) == temp)
            break;
    }
}

// Drain de eventos (game thread)
ByteBuffer buffer = Interlocked.Exchange(ref EventQueue, null);
// Processa toda a lista...
```

### 16.3 Send Queue (Thread-Local + Global)

```csharp
[ThreadStatic]
static ByteBufferPool LocalSendQueue;    // No lock

static ByteBufferPool GlobalSendQueue;   // With lock

// Game thread: writes to local
LocalSendQueue.Add(buffer);

// Game thread: merge to global (at end of tick)
lock (GlobalSendQueue) { GlobalSendQueue.Merge(LocalSendQueue); }

// Signals send thread
SendEvent.Set();

// Send thread: drains global
lock (GlobalSendQueue) { buffer = GlobalSendQueue.Clear(); }
```

