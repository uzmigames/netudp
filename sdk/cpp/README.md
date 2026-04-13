# netudp C++ SDK

Header-only C++17 wrapper for netudp. Zero overhead — all methods are inline and compile to direct C function calls.

## Quick Start

```cpp
#include <netudp.hpp>

int main() {
    netudp::Init guard;  // RAII: netudp_init() / netudp_term()

    uint8_t key[32] = { /* your private key */ };

    netudp::ServerConfig cfg;
    cfg.protocol_id(0x1234567890ABCDEF)
       .private_key(key)
       .channels({{netudp::Channel::Unreliable},
                  {netudp::Channel::ReliableOrdered}});

    netudp::Server server("0.0.0.0:27015", cfg, 0.0);
    server.start(1024);

    while (running) {
        double time = get_time();
        server.update(time);

        // Receive with zero-allocation callback
        server.receive_all(64, [&](netudp::Message msg) {
            printf("client %d ch%d: %d bytes\n",
                   msg.client(), msg.channel(), msg.size());

            // Echo back
            server.send(msg.client(), msg.channel(),
                       msg.data(), msg.size());
        });
    }
}
```

## API Reference

### Lifecycle

| Class | Description |
|-------|-------------|
| `netudp::Init` | RAII guard — `netudp_init()` on construct, `netudp_term()` on destruct |
| `netudp::Server` | Move-only server wrapper |
| `netudp::Client` | Move-only client wrapper |

### Server

```cpp
Server(const char* address, const ServerConfig& config, double time);
void start(int max_clients);
void stop();
void update(double time);
int  max_clients() const;
bool valid() const;

// Send
int  send(int client, int channel, const void* data, int bytes, int flags = 0);
void broadcast(int channel, const void* data, int bytes, int flags = 0);
void broadcast_except(int except, int channel, const void* data, int bytes, int flags = 0);
void flush(int client);

// Zero-copy buffer send
BufferWriter acquire_buffer();
int send_buffer(int client, int channel, BufferWriter& buf, int flags = 0);

// Receive (callback-based, zero allocation)
template<typename Fn> int receive(int client, int max_msgs, Fn&& fn);
template<typename Fn> int receive_all(int max_msgs, Fn&& fn);

// Stats
netudp_server_stats_t stats() const;
int num_io_threads() const;
```

### Client

```cpp
Client(const char* address, const ClientConfig& config, double time);
void connect(uint8_t token[2048]);
void update(double time);
void disconnect();
int  state() const;
bool connected() const;

int  send(int channel, const void* data, int bytes, int flags = 0);
void flush();

template<typename Fn> int receive(int max_msgs, Fn&& fn);
```

### Config Builders

```cpp
ServerConfig cfg;
cfg.protocol_id(0x1234)
   .private_key(key)
   .channels({{Channel::Unreliable},
              {Channel::ReliableOrdered, .priority = 10}})
   .num_io_threads(4)
   .crypto_mode(NETUDP_CRYPTO_AES_GCM);

ClientConfig cli;
cli.protocol_id(0x1234)
   .channels({{Channel::Unreliable}});
```

### BufferWriter / BufferReader

```cpp
// Write (fluent, chainable)
auto buf = server.acquire_buffer();
buf.u8(0x01).f32(x).f32(y).f32(z).string("hello");
server.send_buffer(client, channel, buf);

// Read (from Message)
BufferReader reader(msg_buffer);
uint8_t type = reader.u8();
float x = reader.f32();
```

### Message

```cpp
server.receive_all(64, [](netudp::Message msg) {
    const void* data = msg.data();
    int size = msg.size();
    int channel = msg.channel();
    int client = msg.client();
    const uint8_t* raw = msg.bytes();

    // Type-safe cast
    const MyPacket* pkt = msg.as<MyPacket>();
});
```

### Connect Token

```cpp
uint8_t token[2048];
netudp::generate_connect_token(
    {"game-server.example.com:27015"},
    300, 10, player_id, protocol_id, key, nullptr, token);
```
