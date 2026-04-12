# Spec 20 — Godot 4 GDExtension (sdk/godot)

## Requirements

### REQ-20.1: Extension Structure

```
sdk/godot/
├── netudp.gdextension
├── SConstruct
├── src/
│   ├── register_types.cpp
│   ├── netudp_server.h / .cpp       // GDExtension class wrapping netudp C API
│   ├── netudp_client.h / .cpp
│   ├── netudp_types.h               // Godot-compatible types
│   └── netudp_buffer.h / .cpp       // PackedByteArray integration
└── lib/
    ├── win-x64/netudp.dll
    ├── linux-x64/libnetudp.so
    └── mac-arm64/libnetudp.dylib
```

### REQ-20.2: NetudpServer Class

```cpp
// Exposed to GDScript as "NetudpServer"
class NetudpServerGD : public RefCounted {
    GDCLASS(NetudpServerGD, RefCounted);

protected:
    static void _bind_methods();

public:
    // Lifecycle
    Error start(const String& address, int port, int max_clients);
    void  stop();
    void  update(double time);  // Call from _process()

    // Send
    Error send(int client, int channel, const PackedByteArray& data, int flags = 0);
    Error send_reliable(int client, int channel, const PackedByteArray& data);
    Error broadcast(int channel, const PackedByteArray& data, int flags = 0);
    void  flush(int client);

    // Client management
    void  disconnect_client(int client);
    bool  is_client_connected(int client) const;
    int   get_connected_count() const;

    // Stats
    int   get_ping(int client) const;
    float get_quality(int client) const;

    // Signals (emitted during update())
    // "client_connected"(client_index: int, client_id: int)
    // "client_disconnected"(client_index: int, reason: int)
    // "data_received"(client_index: int, channel: int, data: PackedByteArray)

private:
    netudp_server_t* server_ = nullptr;
};
```

### REQ-20.3: NetudpClient Class

```cpp
class NetudpClientGD : public RefCounted {
    GDCLASS(NetudpClientGD, RefCounted);

public:
    Error connect_to_server(const String& address, const PackedByteArray& token);
    void  update(double time);
    void  disconnect();
    int   get_state() const;
    bool  is_connected() const;
    Error send(int channel, const PackedByteArray& data, int flags = 0);
    int   get_ping() const;

    // Signals
    // "connected"()
    // "disconnected"(reason: int)
    // "data_received"(channel: int, data: PackedByteArray)
};
```

### REQ-20.4: GDScript Usage

```gdscript
extends Node

var server: NetudpServer

func _ready():
    server = NetudpServer.new()
    server.start("0.0.0.0", 7777, 64)
    server.client_connected.connect(_on_client_connected)
    server.client_disconnected.connect(_on_client_disconnected)
    server.data_received.connect(_on_data_received)

func _process(delta):
    server.update(Time.get_unix_time_from_system())

func _on_client_connected(client_index: int, client_id: int):
    print("Player connected: ", client_index)

func _on_data_received(client_index: int, channel: int, data: PackedByteArray):
    var packet_type = data.decode_u8(0)
    match packet_type:
        0x01: _handle_move(client_index, data)
        0x02: _handle_action(client_index, data)

func _handle_move(client: int, data: PackedByteArray):
    var x = data.decode_float(1)
    var y = data.decode_float(5)
    var z = data.decode_float(9)
    # Update player position...

func broadcast_state(data: PackedByteArray):
    server.broadcast(1, data)  # Channel 1, unreliable
```

### REQ-20.5: Signal Integration

SHALL use Godot signals (not callbacks) for event delivery:
- `client_connected(int, int)`
- `client_disconnected(int, int)`
- `data_received(int, int, PackedByteArray)`

Signals emitted during `update()` call. Data `PackedByteArray` is a copy from netudp's internal buffer (Godot requires managed memory for signals).

**Note on `client_id`:** The C API `client_id` is `uint64_t`, but GDScript `int` is signed 64-bit. Client IDs above `INT64_MAX` (e.g., Steam IDs near `UINT64_MAX`) will appear as negative values in GDScript. Consumers should be aware of this and use bitwise operations if unsigned semantics are needed, or restrict client ID space to `[0, INT64_MAX]`.

### REQ-20.6: Connect Token

```gdscript
# Static method to generate token (for dedicated server tools)
var token = NetudpServer.generate_connect_token(
    ["127.0.0.1:7777"],  # Server addresses
    300,                  # Expire seconds
    10,                   # Timeout seconds
    player_id,            # uint64 client ID
    protocol_id,          # uint64 protocol ID
    private_key,          # PackedByteArray (32 bytes)
    user_data             # PackedByteArray (256 bytes, optional)
)
```

## Scenarios

#### Scenario: GDScript echo server
Given a Godot 4 project with netudp GDExtension
When server.start("0.0.0.0", 7777, 64) and _process calls update()
Then data_received signal fires on incoming packets
And server.send() sends response back to client

#### Scenario: PackedByteArray data handling
Given data_received signal with PackedByteArray
When decode_u8(0) reads packet type, decode_float(1) reads X
Then GDScript processes game data natively with Godot types
