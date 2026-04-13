# netudp C++ SDK

Header-only C++17 SDK for netudp. Zero overhead, RAII, typed packets.

- Key auto-generated (no manual `uint8_t[32]`)
- Protocol ID from game name string (no raw hex)
- Ping, handshake, keepalive handled automatically by core
- Built-in logging and profiling wrappers
- Typed packet system with quantization and dispatch

## Quick Start

```cpp
#include <netudp.hpp>

int main() {
    netudp::Init guard;

    // Server — key auto-generated, handshake/ping automatic
    netudp::Server server("0.0.0.0:27015", "my-mmorpg-v1", 1024);

    server.on_connect([](int client, uint64_t id) {
        printf("Player %llu joined (slot %d)\n", id, client);
    });

    while (running) {
        server.update(get_time());

        server.receive_all(64, [&](netudp::Message msg) {
            server.send(msg.client(), 0, msg.data(), msg.size()); // echo
        });
    }
}
```

## Typed Packets

Define game packets as structs (inspired by [ToS-Server-5](https://github.com/andrehrferreira/ToS-Server-5/tree/main/Core/Packets)):

```cpp
struct UpdateEntity {
    static constexpr uint8_t packet_type = 0x01;

    uint32_t entity_id;
    float    pos_x, pos_y, pos_z;
    float    rot_y;
    uint16_t anim_state;

    void serialize(netudp::BufferWriter& buf) const {
        buf.u8(packet_type)
           .u32(entity_id)
           .vec3(pos_x, pos_y, pos_z)   // 6 bytes (quantized int16 x3)
           .rot(rot_y)                   // 1 byte (256 values for 2*pi)
           .u16(anim_state);
    }

    static UpdateEntity deserialize(netudp::RawReader& r) {
        float x, y, z;
        r.vec3(x, y, z);
        return {r.u32(), x, y, z, r.rot(), r.u16()};
    }
};

struct HealthUpdate {
    static constexpr uint8_t packet_type = 0x03;

    uint32_t entity_id;
    uint16_t hp, max_hp;

    void serialize(netudp::BufferWriter& buf) const {
        buf.u8(packet_type).u32(entity_id).u16(hp).u16(max_hp);
    }

    static HealthUpdate deserialize(netudp::RawReader& r) {
        return {r.u32(), r.u16(), r.u16()};
    }
};
```

### Batch Send (multiple packets in one UDP packet)

```cpp
// All packets coalesced into a single encrypted UDP packet:
auto buf = server.acquire_buffer();
UpdateEntity{42, x, y, z, rot, anim}.serialize(buf);
HealthUpdate{42, 85, 100}.serialize(buf);
server.send_buffer(client, 0, buf);
```

### Type-Safe Dispatch

```cpp
netudp::PacketDispatcher dispatch;

dispatch.on<UpdateEntity>([](int client, UpdateEntity pkt) {
    world.set_position(pkt.entity_id, pkt.pos_x, pkt.pos_y, pkt.pos_z);
});

dispatch.on<HealthUpdate>([](int client, HealthUpdate pkt) {
    ui.update_health_bar(pkt.entity_id, pkt.hp, pkt.max_hp);
});

// Raw handler for untyped packets:
dispatch.on_raw(0xFF, [](int client, const uint8_t* data, int size) {
    // raw bytes after the type byte
});

// In game loop:
server.receive_all(64, [&](netudp::Message msg) {
    dispatch.handle(msg);
});
```

## Quantization

Built-in compression for game data:

| Method | Bytes | Full size | Savings |
|--------|------:|----------:|--------:|
| `buf.vec3(x, y, z)` | 6 | 12 (3x float) | **50%** |
| `buf.qf32(value)` | 2 | 4 (float) | **50%** |
| `buf.rot(radians)` | 1 | 4 (float) | **75%** |

```cpp
// Write (quantized)
buf.vec3(100.5f, 200.3f, 50.1f);  // 6 bytes at 0.1 precision
buf.rot(1.57f);                     // 1 byte (256 values)
buf.qf32(health_pct, 0.01f);       // 2 bytes at 0.01 precision

// Read (dequantize)
float x, y, z;
reader.vec3(x, y, z);              // back to float
float angle = reader.rot();         // back to radians
float hp = reader.qf32(0.01f);     // back to float
```

## Server API

```cpp
// Create (key auto-generated, start() called automatically)
Server(address, game_name, max_clients, time = 0.0);

// Advanced: explicit key + channels
Server(address, game_name, key, {{Channel::Unreliable}, {Channel::ReliableOrdered}},
       max_clients, time);

// Lifecycle
void stop();
void update(double time);
int  max_clients() const;
bool valid() const;

// Callbacks
void on_connect(function<void(int client, uint64_t id)>);
void on_disconnect(function<void(int client, int reason)>);

// Send
int  send(int client, int channel, const void* data, int bytes, int flags = 0);
int  send_reliable(int client, const void* data, int bytes);
int  send_unreliable(int client, const void* data, int bytes);
void broadcast(int channel, const void* data, int bytes, int flags = 0);
void broadcast_except(int except, int channel, const void* data, int bytes, int flags = 0);
void flush(int client);

// Zero-copy buffer
BufferWriter acquire_buffer();
int send_buffer(int client, int channel, BufferWriter& buf, int flags = 0);

// Receive (zero allocation)
template<typename Fn> int receive(int client, int max_msgs, Fn&& fn);
template<typename Fn> int receive_all(int max_msgs, Fn&& fn);

// Token generation (uses server's auto-generated key)
bool generate_token(uint64_t client_id, const char* address,
                    uint8_t token_out[2048], int expire = 300, int timeout = 10);

// Stats + threading
netudp_server_stats_t stats() const;
int  num_io_threads() const;
const Key& key() const;
uint64_t get_protocol_id() const;
```

## Client API

```cpp
// Create (game_name must match server)
Client(game_name, time = 0.0);
Client(game_name, {{Channel::Unreliable}, {Channel::ReliableOrdered}}, time);

// Lifecycle
void connect(uint8_t token[2048]);
void update(double time);
void disconnect();
int  state() const;
bool connected() const;

// Send
int  send(int channel, const void* data, int bytes, int flags = 0);
int  send_reliable(const void* data, int bytes);
int  send_unreliable(const void* data, int bytes);
void flush();

// Receive
template<typename Fn> int receive(int max_msgs, Fn&& fn);
```

## Logging & Profiling

```cpp
// Logging
netudp::set_log_level(netudp::LogLevel::Info);
netudp::set_log_callback([](netudp::LogLevel level, const char* file,
                            int line, const char* msg) {
    printf("[%d] %s:%d %s\n", (int)level, file, line, msg);
});

// Profiling
netudp::profiling_enable();
// ... run game loop ...
auto zones = netudp::profiling_get_zones();
for (auto& z : zones) {
    printf("%-30s calls=%llu avg=%.0f ns\n", z.name, z.call_count, z.avg_ns());
}
netudp::profiling_reset();
```

## Protocol ID

Protocol IDs are derived from game name strings via FNV-1a hash. Server and client must use the same game name:

```cpp
// These produce the same protocol_id automatically:
netudp::Server server("0.0.0.0:27015", "my-game-v1", 64);
netudp::Client client("my-game-v1");

// Or compute manually:
uint64_t id = netudp::protocol_id("my-game-v1");
```

## Build

```bash
# With Zig CC (recommended):
zig c++ -std=c++17 -I../../include -I. example.cpp -L../../build/release -lnetudp -o example

# Via project build system:
scripts/build.bat release
```
