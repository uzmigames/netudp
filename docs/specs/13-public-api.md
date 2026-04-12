# Spec 13 — Public API (extern "C")

## Requirements

### REQ-13.1: Lifecycle

```cpp
extern "C" {
int  netudp_init(void);
void netudp_term(void);

// Server
netudp_server_t* netudp_server_create(const char* address,
    const netudp_server_config_t* config, double time);
void netudp_server_start(netudp_server_t* server, int max_clients);
void netudp_server_stop(netudp_server_t* server);
void netudp_server_update(netudp_server_t* server, double time);
void netudp_server_destroy(netudp_server_t* server);

// Client
netudp_client_t* netudp_client_create(const char* address,
    const netudp_client_config_t* config, double time);
void netudp_client_connect(netudp_client_t* client, uint8_t* connect_token);
void netudp_client_update(netudp_client_t* client, double time);
void netudp_client_disconnect(netudp_client_t* client);
void netudp_client_destroy(netudp_client_t* client);
int  netudp_client_state(netudp_client_t* client);
}
```

### REQ-13.2: Send

```cpp
int netudp_server_send(netudp_server_t* server, int client_index,
                       int channel, const void* data, int bytes, int flags);

int netudp_client_send(netudp_client_t* client,
                       int channel, const void* data, int bytes, int flags);

void netudp_server_broadcast(netudp_server_t* server, int channel,
                             const void* data, int bytes, int flags);

void netudp_server_broadcast_except(netudp_server_t* server, int except_client,
                                    int channel, const void* data, int bytes, int flags);

void netudp_server_flush(netudp_server_t* server, int client_index);
void netudp_client_flush(netudp_client_t* client);
```

### REQ-13.3: Receive

```cpp
int netudp_server_receive(netudp_server_t* server, int client_index,
                          netudp_message_t** messages, int max_messages);

int netudp_client_receive(netudp_client_t* client,
                          netudp_message_t** messages, int max_messages);

void netudp_message_release(netudp_message_t* message);
```

### REQ-13.4: Message Struct

```cpp
typedef struct netudp_message {
    void*    data;
    int      size;
    int      channel;
    int      client_index;
    int      flags;
    int64_t  message_number;
    uint64_t receive_time_us;
    // Internal: release function pointer
} netudp_message_t;
```

### REQ-13.5: Send Flags

```cpp
#define NETUDP_SEND_UNRELIABLE    0
#define NETUDP_SEND_RELIABLE      1
#define NETUDP_SEND_NO_NAGLE      2
#define NETUDP_SEND_NO_DELAY      4   // NO_NAGLE + immediate
```

### REQ-13.6: Packet Handler Registration

```cpp
typedef void (*netudp_packet_handler_fn)(void* ctx, int client_index,
    int channel, const uint8_t* data, int len, uint64_t seq);

void netudp_server_set_packet_handler(netudp_server_t* server,
    uint16_t packet_type, netudp_packet_handler_fn handler, void* ctx);
```

### REQ-13.7: Connection Callbacks

```cpp
typedef void (*netudp_connect_fn)(void* ctx, int client_index, uint64_t client_id,
                                   const uint8_t user_data[256]);
typedef void (*netudp_disconnect_fn)(void* ctx, int client_index, int reason);

// Set via server config:
netudp_server_config_t config;
config.callback_context = my_game;
config.on_connect = my_on_connect;
config.on_disconnect = my_on_disconnect;
```

### REQ-13.8: Buffer Acquire/Send

```cpp
netudp_buffer_t* netudp_server_acquire_buffer(netudp_server_t* server);
int netudp_server_send_buffer(netudp_server_t* server, int client_index,
                              int channel, netudp_buffer_t* buf, int flags);

// Buffer write helpers
void     netudp_buffer_write_u8(netudp_buffer_t* buf, uint8_t v);
void     netudp_buffer_write_u16(netudp_buffer_t* buf, uint16_t v);
void     netudp_buffer_write_u32(netudp_buffer_t* buf, uint32_t v);
void     netudp_buffer_write_u64(netudp_buffer_t* buf, uint64_t v);
void     netudp_buffer_write_f32(netudp_buffer_t* buf, float v);
void     netudp_buffer_write_f64(netudp_buffer_t* buf, double v);
void     netudp_buffer_write_varint(netudp_buffer_t* buf, int32_t v);
void     netudp_buffer_write_bytes(netudp_buffer_t* buf, const void* data, int len);
void     netudp_buffer_write_string(netudp_buffer_t* buf, const char* str, int max_len);

// Buffer read helpers
uint8_t  netudp_buffer_read_u8(netudp_buffer_t* buf);
uint16_t netudp_buffer_read_u16(netudp_buffer_t* buf);
uint32_t netudp_buffer_read_u32(netudp_buffer_t* buf);
uint64_t netudp_buffer_read_u64(netudp_buffer_t* buf);
float    netudp_buffer_read_f32(netudp_buffer_t* buf);
double   netudp_buffer_read_f64(netudp_buffer_t* buf);
int32_t  netudp_buffer_read_varint(netudp_buffer_t* buf);
```

### REQ-13.9: Connect Token Generation

```cpp
int netudp_generate_connect_token(
    int num_server_addresses, const char** server_addresses,
    int expire_seconds, int timeout_seconds,
    uint64_t client_id, uint64_t protocol_id,
    const uint8_t private_key[32], uint8_t user_data[256],
    uint8_t connect_token[2048]);
```

### REQ-13.10: Error Codes

```cpp
#define NETUDP_OK                    0
#define NETUDP_ERROR_INVALID_PARAM  -1
#define NETUDP_ERROR_SOCKET         -2
#define NETUDP_ERROR_NO_BUFFERS     -3
#define NETUDP_ERROR_CONNECTION_FULL-4
#define NETUDP_ERROR_NOT_CONNECTED  -5
#define NETUDP_ERROR_MSG_TOO_LARGE  -6
#define NETUDP_ERROR_CRYPTO         -7
#define NETUDP_ERROR_TIMEOUT        -8
#define NETUDP_ERROR_WINDOW_FULL    -9
#define NETUDP_ERROR_COMPRESSION   -10
```

## Scenarios

#### Scenario: Complete server lifecycle
Given valid config with port 27015
When create → start(256) → [update loop] → stop → destroy
Then no memory leaks, no crashes, all connections cleaned up

#### Scenario: Buffer acquire/send pattern
Given server running
When `buf = acquire_buffer(server)`
Then `buf != NULL` and `buf->data` points into pre-allocated pool
When `write_u8(buf, 0x01)` then `write_f32(buf, 3.14)`
Then buf->position == 5
When `send_buffer(server, 0, 1, buf, NETUDP_SEND_UNRELIABLE)`
Then buffer is queued for send and auto-returned to pool after flush
