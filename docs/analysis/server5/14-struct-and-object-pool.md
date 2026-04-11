# 14. StructPool & ObjectPool

## StructPool<T> (NativeMemory)
Fixed-capacity pool with NativeMemory.Alloc. O(N) scan for free slot. `ref T Get(id)` returns reference to slot. Used for entity pools.

## ObjectPool<T> (ConcurrentBag)
Thread-safe pool via ConcurrentBag. Objects implement `IPoolable.Reset()`. Rent/Return pattern.
