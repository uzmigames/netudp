# Buffer and Memory Management

**File:** `GameServer/Shared/Networking/ByteBufferPool.cs` (173 lines)

### 14.1 Pool Architecture (2 tiers)

```
Thread 1 (Game)     Thread 2 (Recv)     Thread 3 (Send)
┌──────────┐        ┌──────────┐        ┌──────────┐
│ Local    │        │ Local    │        │ Local    │
│ Pool     │        │ Pool     │        │ Pool     │
│ (no lock)│        │ (no lock)│        │ (no lock)│
└────┬─────┘        └────┬─────┘        └────┬─────┘
     │ Merge()           │ Merge()           │ Merge()
     ▼                   ▼                   ▼
   ┌─────────────────────────────────────────┐
   │          Global Pool (locked)           │
   │     Intrusive linked list (Head/Tail)   │
   └─────────────────────────────────────────┘
```

### 14.2 Operations

```csharp
// Acquire: local → global → new
public static ByteBuffer Acquire() {
    if (Local == null) {
        lock (Global) { buffer = Global.Take(); }
    } else {
        buffer = Local.Take();
        if (buffer == null) { lock (Global) { buffer = Global.Take(); } }
    }
    if (buffer == null) { buffer = new ByteBuffer(); }  // Fallback: aloca novo
    return buffer;
}

// Release: always to local (no lock)
public static void Release(ByteBuffer buffer) {
    if (Local == null) Local = new ByteBufferPool();
    buffer.Reset();
    Local.Add(buffer);
}

// Merge: transfere local → global (chamado periodicamente)
public static void Merge() {
    if (Local != null && Local.Head != null) {
        lock (Global) { Global.Merge(Local); }
    }
}
```

### 14.3 ByteBuffer — Native Allocation

```csharp
public unsafe class ByteBuffer {
    public byte* Data;  // Pointer to native memory

    public ByteBuffer() {
        Data = (byte*)NativeMemory.Alloc(Connection.Mtu * 3);  // 1200 * 3 = 3600 bytes
    }

    ~ByteBuffer() {
        NativeMemory.Free(Data);  // Freed by finalizer
    }
}
```

**Size of each buffer:** 3600 bytes (3× MTU to accommodate crypto overhead)

**Monitoring:** Every 10 allocations, prints `"Allocated buffer count: N"` to detect leaks.

