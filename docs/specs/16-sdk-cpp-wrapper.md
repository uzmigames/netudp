# Spec 16 — C++ Header-Only Wrapper (sdk/cpp)

## Requirements

### REQ-16.1: RAII Server

```cpp
namespace netudp {

class Server {
public:
    Server(const char* address, const ServerConfig& config, double time);
    ~Server();  // Calls netudp_server_destroy

    Server(Server&& other) noexcept;
    Server& operator=(Server&& other) noexcept;
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void start(int max_clients);
    void stop();
    void update(double time);

    int  send(int client, int channel, std::span<const uint8_t> data, int flags = 0);
    void broadcast(int channel, std::span<const uint8_t> data, int flags = 0);
    void broadcast_except(int except, int channel, std::span<const uint8_t> data, int flags = 0);
    void flush(int client);

    // Receives messages via callback (zero-allocation hot path).
    // The callback is invoked for each message; Message is valid only during the callback.
    void receive(int client, int max, std::function<void(Message&&)> callback);

    // Convenience: collects into vector (allocates — use callback version in hot path).
    std::vector<Message> receive(int client, int max = 64);

    void disconnect_client(int client);
    bool client_connected(int client) const;
    int  num_connected() const;

    ConnectionStats connection_stats(int client) const;
    ChannelStats    channel_stats(int client, int channel) const;
    ServerStats     server_stats() const;

    // Callbacks (safe: stored as std::function, called during update())
    void on_connect(std::function<void(int client, uint64_t id, std::span<const uint8_t> user_data)>);
    void on_disconnect(std::function<void(int client, int reason)>);

    netudp_server_t* handle() const;  // Access raw C handle

private:
    netudp_server_t* handle_ = nullptr;
};

}
```

### REQ-16.2: RAII Client

```cpp
namespace netudp {

class Client {
public:
    Client(const char* address, const ClientConfig& config, double time);
    ~Client();

    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void connect(std::span<uint8_t, 2048> token);
    void update(double time);
    void disconnect();

    int  state() const;
    bool connected() const;

    int  send(int channel, std::span<const uint8_t> data, int flags = 0);
    void flush();

    // Receives messages via callback (zero-allocation hot path).
    void receive(int max, std::function<void(Message&&)> callback);

    // Convenience: collects into vector (allocates — use callback version in hot path).
    std::vector<Message> receive(int max = 64);

    ConnectionStats stats() const;
};

}
```

### REQ-16.3: RAII Message

```cpp
namespace netudp {

class Message {
public:
    Message(netudp_message_t* msg);
    ~Message();  // Calls netudp_message_release

    Message(Message&&) noexcept;
    Message& operator=(Message&&) noexcept;
    Message(const Message&) = delete;

    std::span<const uint8_t> data() const;
    int      size() const;
    int      channel() const;
    int      client() const;
    int64_t  sequence() const;
    uint64_t receive_time_us() const;

private:
    netudp_message_t* msg_ = nullptr;
};

}
```

### REQ-16.4: Buffer Writer (Fluent API)

```cpp
namespace netudp {

class BufferWriter {
public:
    BufferWriter(Server& server);  // Acquires from pool
    ~BufferWriter();               // Releases if not sent

    BufferWriter& u8(uint8_t v);
    BufferWriter& u16(uint16_t v);
    BufferWriter& u32(uint32_t v);
    BufferWriter& u64(uint64_t v);
    BufferWriter& f32(float v);
    BufferWriter& f64(double v);
    BufferWriter& varint(int32_t v);
    BufferWriter& bytes(std::span<const uint8_t> data);
    BufferWriter& string(std::string_view str);

    int send(int client, int channel, int flags = 0);
    int broadcast(int channel, int flags = 0);

    int position() const;

private:
    Server* server_;
    netudp_buffer_t* buf_;
};

// Usage:
BufferWriter(server).u8(0x01).f32(x).f32(y).f32(z).send(client, 1, NETUDP_SEND_UNRELIABLE);

}
```

### REQ-16.5: Buffer Reader

```cpp
namespace netudp {

class BufferReader {
public:
    BufferReader(const Message& msg);
    BufferReader(std::span<const uint8_t> data);

    uint8_t  u8();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    float    f32();
    double   f64();
    int32_t  varint();
    std::span<const uint8_t> bytes(int len);
    std::string_view string();

    int  position() const;
    int  remaining() const;
    bool has_data() const;

private:
    const uint8_t* data_;
    int size_;
    int pos_ = 0;
};

}
```

### REQ-16.6: Connect Token Helper

```cpp
namespace netudp {

struct ConnectToken {
    std::array<uint8_t, 2048> data;

    static std::optional<ConnectToken> generate(
        std::span<const std::string_view> server_addresses,
        int expire_seconds, int timeout_seconds,
        uint64_t client_id, uint64_t protocol_id,
        std::span<const uint8_t, 32> private_key,
        std::span<const uint8_t, 256> user_data = {}
    );
};

}
```

### REQ-16.7: Header File Structure

Single header: `sdk/cpp/include/netudp.hpp`.
Includes `<netudp/netudp.h>` and wraps everything in `namespace netudp`.
No additional compilation needed — header-only.

## Scenarios

#### Scenario: RAII lifecycle
Given a `netudp::Server` constructed on stack
When it goes out of scope
Then `netudp_server_destroy` is called automatically
And no memory leaks

#### Scenario: Move semantics
Given `Server s1("...", cfg, 0.0);`
When `Server s2 = std::move(s1);`
Then `s1.handle() == nullptr` and `s2.handle()` is valid

#### Scenario: Fluent buffer write + send
Given a connected server
When `BufferWriter(server).u8(0x10).f32(1.0f).f32(2.0f).send(0, 1);`
Then 9 bytes sent to client 0 on channel 1 unreliable
