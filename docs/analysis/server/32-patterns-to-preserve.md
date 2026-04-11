# Patterns to Preserve in netudp

Things that **worked well** in production and should be preserved:

| # | Pattern | Where | Why keep |
|---|--------|------|----------------|
| 32.1 | **Packet batching** (Begin/End) | `Connection.cs` | Reduces UDP/CRC overhead from 32+ bytes to 0 per extra message |
| 32.2 | **Buffer pool thread-local + global** | `ByteBufferPool.cs` | Proven zero-alloc in hot path |
| 32.3 | **Fila lock-free CAS** | `NetManager.cs` | Game thread never blocks |
| 32.4 | **Separate send thread** with flush | `NetManager.cs` | Decouples game tick from I/O |
| 32.5 | **CRC32C with hardware** (SSE4.2/ARM) | `CRC32C.cs` | Maximum throughput |
| 32.6 | **VarInt ZigZag** | `ByteBuffer.cs` | 1-5 bytes vs 4 fixed for most values |
| 32.7 | **Symbol table/string interning** | `ByteBuffer.cs` | ~83% savings on repeated strings |
| 32.8 | **Position delta + quantization** | `ByteBuffer.cs` | 2-4 bytes vs 8 bytes per position |
| 32.9 | **Ponteiro direto (unsafe)** para I/O | `ByteBuffer.cs` | Maps directly to C — no overhead |
| 32.10 | **1 NetManager por Scene** | `Scene.cs` | State isolation per map |
| 32.11 | **NativeMemory.Alloc** para buffers | `ByteBuffer.cs` | No GC pressure |
| 32.12 | **Intrusive linked list** para conexoes | `Connection.cs` | Zero-alloc iteration |
| 32.13 | **IHeaderWriter** customizavel | `Connection.cs` | Allows specific header per connection type |
| 32.14 | **EntityTickUpdate ACK implicito** | `EntityTickUpdatePacketHandler.cs` | Confirma estado unreliable no overhead reliable |
| 32.15 | **Rotacao em 1 byte** | `ByteBuffer.cs` | 256 values sufficient for 2D |
