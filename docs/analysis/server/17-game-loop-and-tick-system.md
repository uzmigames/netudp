# Game Loop and Tick System

**File:** `GameServer/Server/Scene/Scene.TaskScheduler.cs` (162 lines), `Scene.cs` (linha 1537)

### 17.1 Main Loop

```csharp
private void GameLoop() {
    Stopwatch stopwatch = Stopwatch.StartNew();
    TimeSpan frameTime = TimeSpan.FromSeconds(1.0 / 32.0);  // 31.25ms
    TimeSpan targetTime = frameTime;

    while (true) {
        // Drena tasks assincronas (DB, etc)
        while (TaskQueue.TryDequeue(out Task task))
            TryExecuteTask(task);

        // Tick do jogo
        if (!Tick()) break;

        // Sleep preciso
        int sleepTime = (targetTime - stopwatch.Elapsed).Milliseconds;
        if (sleepTime > 1) Thread.Sleep(sleepTime - 1);
        targetTime += frameTime;
    }
}
```

### 17.2 Tick

```csharp
public bool Tick() {
    Manager.ProcessEvents();         // 1. Drains network packets
    TimerQueue.Update();             // 2. Timers (cooldowns, delays)
    for (updateable...) Update();    // 3. Entities (AI, movement, combat)
    for (pc...) pc.Update();         // 4. PlayerControllers (AoI, outgoing packets)
    Manager.Update(DeltaTime);       // 5. Flush buffers, check timeouts
    ConcurrentByteBufferPool.Merge();// 6. Recycle buffers
    return !IsFinalized;
}
```

### 17.3 Custom TaskScheduler

The `Scene` **is** a .NET `TaskScheduler`. This enables:
- `await` in game code (DB queries, etc.) without blocking the game thread
- Continuations executed on the game thread (not the thread pool)
- `TaskFactory.StartNew(GameLoop, TaskCreationOptions.LongRunning)` to start

