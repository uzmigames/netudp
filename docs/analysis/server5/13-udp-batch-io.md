# 13. UDP Batch I/O

**File:** `Core/Network/UdpBatchIO.cs` (134 lines)

Batch receive: loop `UDP.Poll(0)` + `Receive` up to maxMessages with callback. Batch send: multiple overloads for tuples, PacketPointer (unsafe), PacketMemory (Memory<byte>). Maps to recvmmsg/sendmmsg on Linux for netudp.
