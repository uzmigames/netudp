# 15. QueueStructLinked

**File:** `Core/Utils/QueueStructLinked.cs` (141 lines)

Evolution of Server1's ByteBufferPool. Adds node pooling via ConcurrentStack (max 1024 nodes). Same Head/Tail/Add/Take/Merge/Clear API. Generic type T (vs Server1's ByteBuffer-only). ReleaseChain() returns entire chain to pool. Zero-allocation after warmup.
