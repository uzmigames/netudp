# 9. Public API Design

## Initialization

```c
int netcode_init();    // Initialize (winsock, sodium)
void netcode_term();   // Cleanup
```

## Client API

```c
// Create/destroy
netcode_client_t * netcode_client_create(const char * address, const netcode_client_config_t * config, double time);
void netcode_client_destroy(netcode_client_t * client);

// Connect/disconnect
void netcode_client_connect(netcode_client_t * client, uint8_t * connect_token);
void netcode_client_disconnect(netcode_client_t * client);

// Update (call every frame)
void netcode_client_update(netcode_client_t * client, double time);

// Send/receive
void netcode_client_send_packet(netcode_client_t * client, const uint8_t * data, int bytes);
uint8_t * netcode_client_receive_packet(netcode_client_t * client, int * bytes, uint64_t * sequence);
void netcode_client_free_packet(netcode_client_t * client, void * packet);

// State
int netcode_client_state(netcode_client_t * client);  // returns NETCODE_CLIENT_STATE_*
```

## Server API

```c
// Create/destroy
netcode_server_t * netcode_server_create(const char * address, const netcode_server_config_t * config, double time);
void netcode_server_destroy(netcode_server_t * server);

// Start/stop
void netcode_server_start(netcode_server_t * server, int max_clients);
void netcode_server_stop(netcode_server_t * server);

// Update (call every frame)
void netcode_server_update(netcode_server_t * server, double time);

// Send/receive (per client index)
void netcode_server_send_packet(netcode_server_t * server, int client_index, const uint8_t * data, int bytes);
uint8_t * netcode_server_receive_packet(netcode_server_t * server, int client_index, int * bytes, uint64_t * sequence);
void netcode_server_free_packet(netcode_server_t * server, void * packet);

// Client management
int netcode_server_client_connected(netcode_server_t * server, int client_index);
void netcode_server_disconnect_client(netcode_server_t * server, int client_index);
int netcode_server_num_connected_clients(netcode_server_t * server);
```

## Config Structs

```c
struct netcode_client_config_t {
    void * allocator_context;
    void * (*allocate_function)(void*, size_t);
    void (*free_function)(void*, void*);
    netcode_network_simulator_t * network_simulator;
    void * callback_context;
    void (*state_change_callback)(void*, int old_state, int new_state);
    void (*send_loopback_packet_callback)(void*, int, const uint8_t*, int, uint64_t);
    int override_send_and_receive;
    void (*send_packet_override)(void*, netcode_address_t*, const uint8_t*, int);
    int (*receive_packet_override)(void*, netcode_address_t*, uint8_t*, int);
};

struct netcode_server_config_t {
    uint64_t protocol_id;
    uint8_t private_key[32];     // Shared with web backend
    // ... same allocator/callback pattern as client
};
```

## Key Constants

```c
#define NETCODE_CONNECT_TOKEN_BYTES       2048
#define NETCODE_KEY_BYTES                 32
#define NETCODE_MAC_BYTES                 16
#define NETCODE_USER_DATA_BYTES           256
#define NETCODE_MAX_SERVERS_PER_CONNECT   32
#define NETCODE_MAX_CLIENTS               256
#define NETCODE_MAX_PACKET_SIZE           1200
#define NETCODE_REPLAY_PROTECTION_BUFFER_SIZE  256
#define NETCODE_PACKET_SEND_RATE          10.0   // 10 Hz keepalive
#define NETCODE_NUM_DISCONNECT_PACKETS    10     // Redundant disconnect sends
```

## Design Patterns Worth Adopting

1. **Opaque types** — `netcode_client_t *` / `netcode_server_t *`, internal structs hidden
2. **Explicit time** — `double time` passed to update, no internal clock
3. **Custom allocator** — pluggable allocate/free functions
4. **Callbacks for state changes** — `state_change_callback`
5. **Network simulator** — built-in for testing
6. **Loopback mode** — local client/server without real sockets
7. **Send/receive override** — pluggable transport for testing
8. **free_packet pattern** — application must free received packets explicitly
