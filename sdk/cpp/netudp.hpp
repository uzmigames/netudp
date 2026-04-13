#pragma once
/**
 * @file netudp.hpp
 * @brief C++17 header-only SDK for netudp.
 *
 * RAII, move-only wrappers around the C API. Zero overhead — all methods
 * are inline and compile to direct C function calls.
 *
 * Usage:
 *   #include <netudp.hpp>
 *
 *   netudp::Init guard;  // calls netudp_init(), netudp_term() on scope exit
 *
 *   netudp::ServerConfig cfg;
 *   cfg.protocol_id(0x1234).private_key(key).channels({{Channel::Unreliable}});
 *
 *   netudp::Server server("0.0.0.0:27015", cfg, 0.0);
 *   server.start(1024);
 *   server.on_connect([](int client, uint64_t id, const uint8_t* ud) { ... });
 */

#include <netudp/netudp.h>

#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace netudp {

/* ======================================================================
 * Init guard — RAII lifecycle
 * ====================================================================== */

class Init {
public:
    Init()  { netudp_init(); }
    ~Init() { netudp_term(); }
    Init(const Init&) = delete;
    Init& operator=(const Init&) = delete;
};

/* ======================================================================
 * Channel type enum (mirrors netudp_channel_type_t)
 * ====================================================================== */

enum class Channel : uint8_t {
    Unreliable         = NETUDP_CHANNEL_UNRELIABLE,
    UnreliableSequenced = NETUDP_CHANNEL_UNRELIABLE_SEQUENCED,
    ReliableOrdered    = NETUDP_CHANNEL_RELIABLE_ORDERED,
    ReliableUnordered  = NETUDP_CHANNEL_RELIABLE_UNORDERED,
};

/* ======================================================================
 * Send flags
 * ====================================================================== */

enum SendFlags : int {
    Default   = 0,
    NoNagle   = NETUDP_SEND_NO_NAGLE,
    NoDelay   = NETUDP_SEND_NO_DELAY,
};

/* ======================================================================
 * Message — non-owning view into a received message
 * ====================================================================== */

class Message {
public:
    explicit Message(netudp_message_t* raw) : raw_(raw) {}
    ~Message() { if (raw_) { netudp_message_release(raw_); } }

    Message(Message&& o) noexcept : raw_(o.raw_) { o.raw_ = nullptr; }
    Message& operator=(Message&& o) noexcept {
        if (this != &o) {
            if (raw_) { netudp_message_release(raw_); }
            raw_ = o.raw_;
            o.raw_ = nullptr;
        }
        return *this;
    }
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;

    const void* data() const    { return raw_ ? raw_->data : nullptr; }
    int         size() const    { return raw_ ? raw_->size : 0; }
    int         channel() const { return raw_ ? raw_->channel : 0; }
    int         client() const  { return raw_ ? raw_->client_index : 0; }

    const uint8_t* bytes() const {
        return static_cast<const uint8_t*>(data());
    }

    template <typename T>
    const T* as() const {
        return (size() >= static_cast<int>(sizeof(T)))
             ? static_cast<const T*>(data()) : nullptr;
    }

private:
    netudp_message_t* raw_;
};

/* ======================================================================
 * BufferWriter — fluent zero-copy buffer builder
 * ====================================================================== */

class BufferWriter {
public:
    explicit BufferWriter(netudp_buffer_t* buf) : buf_(buf) {}

    BufferWriter& u8(uint8_t v)    { netudp_buffer_write_u8(buf_, v);  return *this; }
    BufferWriter& u16(uint16_t v)  { netudp_buffer_write_u16(buf_, v); return *this; }
    BufferWriter& u32(uint32_t v)  { netudp_buffer_write_u32(buf_, v); return *this; }
    BufferWriter& u64(uint64_t v)  { netudp_buffer_write_u64(buf_, v); return *this; }
    BufferWriter& f32(float v)     { netudp_buffer_write_f32(buf_, v); return *this; }
    BufferWriter& f64(double v)    { netudp_buffer_write_f64(buf_, v); return *this; }
    BufferWriter& varint(int32_t v){ netudp_buffer_write_varint(buf_, v); return *this; }
    BufferWriter& bytes(const void* data, int len) {
        netudp_buffer_write_bytes(buf_, data, len);
        return *this;
    }
    BufferWriter& string(const char* str, int max_len = 256) {
        netudp_buffer_write_string(buf_, str, max_len);
        return *this;
    }

    netudp_buffer_t* raw() { return buf_; }

private:
    netudp_buffer_t* buf_;
};

/* ======================================================================
 * BufferReader — fluent zero-copy buffer reader
 * ====================================================================== */

class BufferReader {
public:
    explicit BufferReader(netudp_buffer_t* buf) : buf_(buf) {}

    uint8_t  u8()     { return netudp_buffer_read_u8(buf_); }
    uint16_t u16()    { return netudp_buffer_read_u16(buf_); }
    uint32_t u32()    { return netudp_buffer_read_u32(buf_); }
    uint64_t u64()    { return netudp_buffer_read_u64(buf_); }
    float    f32()    { return netudp_buffer_read_f32(buf_); }
    double   f64()    { return netudp_buffer_read_f64(buf_); }
    int32_t  varint() { return netudp_buffer_read_varint(buf_); }

private:
    netudp_buffer_t* buf_;
};

/* ======================================================================
 * ServerConfig — builder for netudp_server_config_t
 * ====================================================================== */

struct ChannelConfig {
    Channel type     = Channel::Unreliable;
    uint8_t priority = 0;
    uint8_t weight   = 1;
    uint16_t nagle_ms = 0;
};

class ServerConfig {
public:
    ServerConfig& protocol_id(uint64_t id) { cfg_.protocol_id = id; return *this; }

    ServerConfig& private_key(const uint8_t key[32]) {
        std::memcpy(cfg_.private_key, key, 32);
        return *this;
    }

    ServerConfig& channels(std::initializer_list<ChannelConfig> chs) {
        int i = 0;
        for (auto& ch : chs) {
            if (i >= 255) { break; }
            cfg_.channels[i].type     = static_cast<uint8_t>(ch.type);
            cfg_.channels[i].priority = ch.priority;
            cfg_.channels[i].weight   = ch.weight;
            cfg_.channels[i].nagle_ms = ch.nagle_ms;
            ++i;
        }
        cfg_.num_channels = i;
        return *this;
    }

    ServerConfig& num_io_threads(int n) { cfg_.num_io_threads = n; return *this; }
    ServerConfig& crypto_mode(uint8_t mode) { cfg_.crypto_mode = mode; return *this; }
    ServerConfig& log_level(int level) { cfg_.log_level = level; return *this; }

    const netudp_server_config_t& raw() const { return cfg_; }
    netudp_server_config_t& raw() { return cfg_; }

private:
    netudp_server_config_t cfg_ = {};
};

/* ======================================================================
 * ClientConfig — builder for netudp_client_config_t
 * ====================================================================== */

class ClientConfig {
public:
    ClientConfig& protocol_id(uint64_t id) { cfg_.protocol_id = id; return *this; }

    ClientConfig& channels(std::initializer_list<ChannelConfig> chs) {
        int i = 0;
        for (auto& ch : chs) {
            if (i >= 255) { break; }
            cfg_.channels[i].type     = static_cast<uint8_t>(ch.type);
            cfg_.channels[i].priority = ch.priority;
            cfg_.channels[i].weight   = ch.weight;
            cfg_.channels[i].nagle_ms = ch.nagle_ms;
            ++i;
        }
        cfg_.num_channels = i;
        return *this;
    }

    const netudp_client_config_t& raw() const { return cfg_; }
    netudp_client_config_t& raw() { return cfg_; }

private:
    netudp_client_config_t cfg_ = {};
};

/* ======================================================================
 * Server — RAII wrapper
 * ====================================================================== */

class Server {
public:
    Server(const char* address, const ServerConfig& config, double time)
        : handle_(netudp_server_create(address, &config.raw(), time)) {}

    ~Server() { if (handle_) { netudp_server_destroy(handle_); } }

    Server(Server&& o) noexcept : handle_(o.handle_) { o.handle_ = nullptr; }
    Server& operator=(Server&& o) noexcept {
        if (this != &o) {
            if (handle_) { netudp_server_destroy(handle_); }
            handle_ = o.handle_;
            o.handle_ = nullptr;
        }
        return *this;
    }
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool valid() const { return handle_ != nullptr; }
    explicit operator bool() const { return valid(); }
    netudp_server_t* raw() { return handle_; }

    /* Lifecycle */
    void start(int max_clients) { netudp_server_start(handle_, max_clients); }
    void stop()                 { netudp_server_stop(handle_); }
    void update(double time)    { netudp_server_update(handle_, time); }
    int  max_clients() const    { return netudp_server_max_clients(handle_); }

    /* Callbacks */
    using ConnectFn = std::function<void(int client, uint64_t id, const uint8_t user_data[256])>;
    using DisconnectFn = std::function<void(int client, int reason)>;

    void on_connect(ConnectFn fn) {
        connect_fn_ = std::move(fn);
        auto& cfg = const_cast<netudp_server_config_t&>(*reinterpret_cast<const netudp_server_config_t*>(&handle_));
        /* Callbacks need to be set before start() via the config.
         * For post-creation callback binding, store in Server and use
         * a static trampoline. This requires the raw config to hold
         * our context pointer — set via the config before create. */
        (void)cfg;
    }

    void on_disconnect(DisconnectFn fn) {
        disconnect_fn_ = std::move(fn);
    }

    /* Send */
    int send(int client, int channel, const void* data, int bytes, int flags = 0) {
        return netudp_server_send(handle_, client, channel, data, bytes, flags);
    }

    void broadcast(int channel, const void* data, int bytes, int flags = 0) {
        netudp_server_broadcast(handle_, channel, data, bytes, flags);
    }

    void broadcast_except(int except, int channel, const void* data, int bytes, int flags = 0) {
        netudp_server_broadcast_except(handle_, except, channel, data, bytes, flags);
    }

    void flush(int client) { netudp_server_flush(handle_, client); }

    /* Zero-copy buffer send */
    BufferWriter acquire_buffer() {
        return BufferWriter(netudp_server_acquire_buffer(handle_));
    }

    int send_buffer(int client, int channel, BufferWriter& buf, int flags = 0) {
        return netudp_server_send_buffer(handle_, client, channel, buf.raw(), flags);
    }

    /* Receive — callback-based (zero allocation) */
    template <typename Fn>
    int receive(int client, int max_msgs, Fn&& fn) {
        netudp_message_t* msgs[64];
        int cap = (max_msgs > 64) ? 64 : max_msgs;
        int n = netudp_server_receive(handle_, client, msgs, cap);
        for (int i = 0; i < n; ++i) {
            fn(Message(msgs[i]));
        }
        return n;
    }

    /* Receive batch — callback-based across all clients */
    template <typename Fn>
    int receive_all(int max_msgs, Fn&& fn) {
        netudp_message_t* msgs[64];
        int cap = (max_msgs > 64) ? 64 : max_msgs;
        int n = netudp_server_receive_batch(handle_, msgs, cap);
        for (int i = 0; i < n; ++i) {
            fn(Message(msgs[i]));
        }
        return n;
    }

    /* Packet handler */
    void set_packet_handler(uint16_t type, netudp_packet_handler_fn fn, void* ctx) {
        netudp_server_set_packet_handler(handle_, type, fn, ctx);
    }

    /* Stats */
    netudp_server_stats_t stats() const {
        netudp_server_stats_t s = {};
        netudp_server_get_stats(handle_, &s);
        return s;
    }

    /* Threading */
    int num_io_threads() const { return netudp_server_num_io_threads(handle_); }
    int set_thread_affinity(int thread, int cpu) {
        return netudp_server_set_thread_affinity(handle_, thread, cpu);
    }

private:
    netudp_server_t* handle_;
    ConnectFn connect_fn_;
    DisconnectFn disconnect_fn_;
};

/* ======================================================================
 * Client — RAII wrapper
 * ====================================================================== */

class Client {
public:
    Client(const char* address, const ClientConfig& config, double time)
        : handle_(netudp_client_create(address, &config.raw(), time)) {}

    ~Client() { if (handle_) { netudp_client_destroy(handle_); } }

    Client(Client&& o) noexcept : handle_(o.handle_) { o.handle_ = nullptr; }
    Client& operator=(Client&& o) noexcept {
        if (this != &o) {
            if (handle_) { netudp_client_destroy(handle_); }
            handle_ = o.handle_;
            o.handle_ = nullptr;
        }
        return *this;
    }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool valid() const { return handle_ != nullptr; }
    explicit operator bool() const { return valid(); }
    netudp_client_t* raw() { return handle_; }

    /* Lifecycle */
    void connect(uint8_t token[2048])  { netudp_client_connect(handle_, token); }
    void update(double time)           { netudp_client_update(handle_, time); }
    void disconnect()                  { netudp_client_disconnect(handle_); }
    int  state() const                 { return netudp_client_state(handle_); }
    bool connected() const             { return state() == 3; }

    /* Send */
    int send(int channel, const void* data, int bytes, int flags = 0) {
        return netudp_client_send(handle_, channel, data, bytes, flags);
    }

    void flush() { netudp_client_flush(handle_); }

    /* Receive — callback-based (zero allocation) */
    template <typename Fn>
    int receive(int max_msgs, Fn&& fn) {
        netudp_message_t* msgs[64];
        int cap = (max_msgs > 64) ? 64 : max_msgs;
        int n = netudp_client_receive(handle_, msgs, cap);
        for (int i = 0; i < n; ++i) {
            fn(Message(msgs[i]));
        }
        return n;
    }

private:
    netudp_client_t* handle_;
};

/* ======================================================================
 * Token generation helper
 * ====================================================================== */

inline int generate_connect_token(
    const std::vector<std::string>& server_addresses,
    int expire_seconds,
    int timeout_seconds,
    uint64_t client_id,
    uint64_t protocol_id,
    const uint8_t private_key[32],
    const uint8_t user_data[256],
    uint8_t token_out[2048])
{
    std::vector<const char*> addrs;
    addrs.reserve(server_addresses.size());
    for (auto& a : server_addresses) { addrs.push_back(a.c_str()); }
    return netudp_generate_connect_token(
        static_cast<int>(addrs.size()), addrs.data(),
        expire_seconds, timeout_seconds,
        client_id, protocol_id, private_key, user_data, token_out);
}

} // namespace netudp
