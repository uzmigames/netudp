# Spec 19 — Unity C# Bindings (sdk/unity)

## Requirements

### REQ-19.1: Package Structure

```
sdk/unity/com.uzmigames.netudp/
├── package.json
├── Runtime/
│   ├── com.uzmigames.netudp.asmdef
│   ├── Netudp.cs                    // Main P/Invoke bindings
│   ├── NetudpServer.cs              // High-level server wrapper
│   ├── NetudpClient.cs              // High-level client wrapper
│   ├── NetudpTypes.cs               // Structs, enums, constants
│   ├── NetudpBuffer.cs              // Buffer read/write helpers
│   └── Plugins/
│       ├── x86_64/netudp.dll        // Windows
│       ├── x86_64/libnetudp.so      // Linux
│       └── arm64/libnetudp.dylib    // macOS
├── Editor/
│   └── NetudpSettingsProvider.cs    // Inspector UI
└── Tests/
    └── NetudpTests.cs
```

### REQ-19.2: P/Invoke Bindings

```csharp
// Runtime/Netudp.cs
public static class Netudp
{
    private const string DLL = "netudp";

    [DllImport(DLL)] public static extern int netudp_init();
    [DllImport(DLL)] public static extern void netudp_term();

    [DllImport(DLL)] public static extern IntPtr netudp_server_create(
        string address, ref NetudpServerConfig config, double time);
    [DllImport(DLL)] public static extern void netudp_server_start(IntPtr server, int maxClients);
    [DllImport(DLL)] public static extern void netudp_server_update(IntPtr server, double time);
    [DllImport(DLL)] public static extern void netudp_server_stop(IntPtr server);
    [DllImport(DLL)] public static extern void netudp_server_destroy(IntPtr server);

    [DllImport(DLL)] public static extern int netudp_server_send(
        IntPtr server, int clientIndex, int channel,
        byte[] data, int bytes, int flags);

    [DllImport(DLL)] public static extern int netudp_server_send(
        IntPtr server, int clientIndex, int channel,
        IntPtr data, int bytes, int flags);  // NativeArray overload

    [DllImport(DLL)] public static extern unsafe int netudp_server_receive(
        IntPtr server, int clientIndex,
        netudp_message_t** messages, int maxMessages);

    [DllImport(DLL)] public static extern void netudp_message_release(IntPtr message);

    [DllImport(DLL)] public static extern int netudp_generate_connect_token(
        int numServers, string[] servers, int expire, int timeout,
        ulong clientId, ulong protocolId,
        byte[] privateKey, byte[] userData, byte[] outToken);
}
```

### REQ-19.3: High-Level Server (Zero-GC Friendly)

```csharp
// Runtime/NetudpServer.cs
public class NetudpServer : IDisposable
{
    private IntPtr _handle;

    public void Start(string address, int port, int maxClients, NetudpServerConfig config)
    {
        _handle = Netudp.netudp_server_create($"{address}:{port}", ref config, Time.realtimeSinceStartupAsDouble);
        Netudp.netudp_server_start(_handle, maxClients);
    }

    // Called from MonoBehaviour.Update() — NO managed allocations
    public void Tick()
    {
        Netudp.netudp_server_update(_handle, Time.realtimeSinceStartupAsDouble);
    }

    // Send with NativeArray (zero-copy, no GC)
    public void Send(int client, int channel, NativeArray<byte> data, int flags = 0)
    {
        unsafe
        {
            Netudp.netudp_server_send(_handle, client, channel,
                (IntPtr)data.GetUnsafeReadOnlyPtr(), data.Length, flags);
        }
    }

    // Send with byte[] (copies, but simpler API)
    public void Send(int client, int channel, byte[] data, int flags = 0)
    {
        Netudp.netudp_server_send(_handle, client, channel, data, data.Length, flags);
    }

    public void Dispose()
    {
        if (_handle != IntPtr.Zero)
        {
            Netudp.netudp_server_destroy(_handle);
            _handle = IntPtr.Zero;
        }
    }
}
```

### REQ-19.4: Zero-GC Pattern

For hot-path operations (Tick, Send, Receive):
- SHALL use `NativeArray<byte>` for buffer passing (no managed allocation)
- SHALL NOT allocate managed arrays in the send/receive loop
- SHALL pre-allocate `NativeArray<IntPtr>` for message pointers

```csharp
// Zero-GC receive pattern:
private NativeArray<IntPtr> _msgPtrs;  // Pre-allocated in Start()

public void PollMessages(int client, Action<int, int, NativeSlice<byte>> onMessage)
{
    unsafe
    {
        int count = Netudp.netudp_server_receive(_handle, client,
            (netudp_message_t**)_msgPtrs.GetUnsafePtr(), _msgPtrs.Length);

        for (int i = 0; i < count; i++)
        {
            var msg = (netudp_message_t*)_msgPtrs[i];
            var data = NativeSliceUnsafeUtility.ConvertExistingDataToNativeSlice<byte>(
                msg->data, 1, msg->size);
            onMessage(msg->channel, msg->client_index, data);
            Netudp.netudp_message_release((IntPtr)msg);
        }
    }
}
```

### REQ-19.5: Events via C# Delegates

```csharp
public event Action<int, ulong> OnClientConnected;
public event Action<int, int> OnClientDisconnected;
public event Action<int, int, NativeSlice<byte>> OnDataReceived;
```

Fired during `Tick()` from native callbacks.

## Scenarios

#### Scenario: Unity dedicated server
Given Unity project with com.uzmigames.netudp package
When Start("::", 7777, 64, config) called
Then server starts with zero managed allocations after init
And Tick() every Update() drives network I/O with no GC

#### Scenario: NativeArray send (zero-GC)
Given NativeArray<byte> with 64 bytes of game state
When Send(client, 1, nativeData, UNRELIABLE)
Then data sent without any managed array copy

#### Scenario: Connect token from backend
Given REST API returns base64 connect token
When decoded to byte[2048] and passed to ConnectToServer()
Then client connects using netudp protocol
