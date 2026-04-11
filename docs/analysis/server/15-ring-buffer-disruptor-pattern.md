# Ring Buffer (Disruptor Pattern)

**File:** `GameServer/Shared/Networking/RingBuffer.cs` (273 lines)

### 15.1 Lock-Free SPSC Queue

```csharp
public class RingBuffer<T> {
    private readonly T[] _entries;
    private readonly int _modMask;       // capacity - 1 (power of two)
    private Volatile.PaddedLong _consumerCursor;
    private Volatile.PaddedLong _producerCursor;
}
```

### 15.2 Cache-Line Padding

```csharp
[StructLayout(LayoutKind.Explicit, Size = 128)]  // 2 cache lines
public struct PaddedLong {
    [FieldOffset(64)]  // Value in the middle — avoids false sharing
    private long _value;

    public long ReadAcquireFence() {
        var value = _value;
        Thread.MemoryBarrier();
        return value;
    }

    public void WriteReleaseFence(long newValue) {
        Thread.MemoryBarrier();
        _value = newValue;
    }
}
```

### 15.3 Enqueue/Dequeue

```csharp
public void Enqueue(T item) {
    var next = _producerCursor.ReadAcquireFence() + 1;
    long wrapPoint = next - _entries.Length;
    while (wrapPoint > _consumerCursor.ReadAcquireFence())
        Thread.SpinWait(1);  // Waits if full
    this[next] = item;
    _producerCursor.WriteReleaseFence(next);
}

public T Dequeue() {
    var next = _consumerCursor.ReadAcquireFence() + 1;
    while (_producerCursor.ReadAcquireFence() < next)
        Thread.SpinWait(1);  // Waits if empty
    var result = this[next];
    _consumerCursor.WriteReleaseFence(next);
    return result;
}
```

