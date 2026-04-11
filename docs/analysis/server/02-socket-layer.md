# Socket Layer (UDP)

### 2.1 NanoSockets (Primary Backend)

**File:** `GameServer/Shared/Networking/NanoSockets.cs` (218 lines)

C# P/Invoke wrapper over the C library `nanosockets` de Stanislav Denisov (MIT).

**Functions used:**
```csharp
UDP.Initialize()                          // Initialize Winsock/etc
UDP.Create(sendBufSize, recvBufSize)      // Create UDP socket
UDP.Bind(socket, ref address)             // Bind to address
UDP.Connect(socket, ref address)          // "Connect" (set default destination)
UDP.Poll(socket, timeoutMs)               // Check for data (select/poll)
UDP.SetNonBlocking(socket)                // Non-blocking mode
UDP.Unsafe.Receive(socket, &addr, buf, len)  // recvfrom with raw pointers
UDP.Unsafe.Send(socket, &addr, buf, len)     // sendto with raw pointers
UDP.Destroy(ref socket)                   // Close socket
UDP.SetIP(ref address, "::0")            // Set IP (uses IPv6 dual-stack)
UDP.GetIP(ref address, sb, len)           // Read IP as string
```

**Struct Address:**
```csharp
[StructLayout(LayoutKind.Explicit, Size = 18)]
public struct Address : IEquatable<Address> {
    [FieldOffset(0)]  ulong address0;   // 8 bytes
    [FieldOffset(8)]  ulong address1;   // 8 bytes
    [FieldOffset(16)] ushort port;      // 2 bytes
}
// Total: 18 bytes — compact, hashable, comparable
```

**Socket buffer configuration:**
- Send buffer: **512 KB** (`512 * 1024`)
- Receive buffer: **512 KB** (`512 * 1024`)
- Bind em `::0` (IPv6 dual-stack, aceita IPv4 e IPv6)

### 2.2 NativeSocket (Alternative Backend)

**File:** `GameServer/Shared/Networking/NativeSocket.cs` (273 lines)

Direct P/Invoke to `ws2_32.dll` (Windows) and `libc` (Linux). Used as an alternative to NanoSockets.

**Struct NativeAddr:**
```csharp
[StructLayout(LayoutKind.Explicit, Size = 32)]
public struct NativeAddr : IEquatable<NativeAddr> {
    [FieldOffset(0)]  long _part1;  // family, port, etc
    [FieldOffset(8)]  long _part2;
    [FieldOffset(16)] long _part3;  // IPv6 parts
    [FieldOffset(24)] int  _part4;
    [FieldOffset(28)] int  _hash;   // pre-computed hash
}
// Total: 32 bytes — suporta IPv4 (16 bytes) e IPv6 (28 bytes)
// Hash computed in Initialize() for Dictionary usage
```

**Platform detection:**
```csharp
static NativeSocket() {
    if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))  { IsSupported = true; UnixMode = true; }
    else if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) { IsSupported = true; }
}
```

**Error mapping:** 30+ native Unix errors mapped to `SocketError` do .NET.

