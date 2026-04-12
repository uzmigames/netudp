# Spec 17 — UzEngine Integration (sdk/uzengine)

## Requirements

### REQ-17.1: NetworkingSubsystem

```cpp
// engine/networking/networking_subsystem.h
namespace uz {

class NetworkingSubsystem : public ISubsystem {
public:
    const char* GetName() const override;                   // "Networking"
    InitPhase   GetInitPhase() const override;              // PreInit
    TickGroup   GetTickGroup() const override;              // Update
    std::vector<const char*> GetDependencies() const override; // {}

    bool Init() override;        // netudp_init(), log SIMD level
    void Shutdown() override;    // netudp_server_destroy(), netudp_term()
    void Tick(float dt) override; // netudp_server_update(), poll events, flush EventQueue
};

}
```

SHALL inherit from `uz::ISubsystem` and follow UzEngine lifecycle conventions.
SHALL log via `UZ_LOG(Networking, ...)`.
SHALL profile via `UZ_PROFILE_SCOPE("Networking::Tick")`.

### REQ-17.2: Memory Integration

SHALL route netudp allocations through UzEngine allocator:

```cpp
cfg.allocator_context = nullptr;
cfg.allocate_function = [](void*, size_t bytes) -> void* { return uz_malloc(bytes); };
cfg.free_function = [](void*, void* ptr) { uz_free(ptr); };
```

Pre-connection pools SHALL use `uz::PoolAllocator<T>` pattern.

### REQ-17.3: Event Dispatch

SHALL use `uz::EventQueue<NetworkEvent>` for frame-paced delivery:

```cpp
struct NetworkEvent {
    enum class Type : uint8_t { ClientConnected, ClientDisconnected, DataReceived };
    Type        type;
    uint32_t    client_index;
    uint64_t    client_id;
    int         channel;
    int         disconnect_reason;
    const uint8_t* data;     // Valid only during Flush()
    int         data_size;
    uint64_t    sequence;
};
```

Events SHALL be enqueued during `PollServerEvents()` and flushed at end of `Tick()`.
Data pointers SHALL be valid only during `EventQueue::Flush()` callback scope.

### REQ-17.4: Server API

```cpp
namespace uz {

class NetworkingSubsystem : public ISubsystem {
    // ... ISubsystem interface above ...

    // Server lifecycle
    bool StartServer(const NetworkConfig& config);
    void StopServer();
    bool IsServerRunning() const;

    // Client lifecycle
    bool ConnectToServer(const char* address, uint8_t* connect_token);
    void DisconnectFromServer();
    bool IsConnected() const;

    // Send (server-side)
    void Send(int client, int channel, const uint8_t* data, int size, int flags = 0);
    void SendReliable(int client, int channel, const uint8_t* data, int size);
    void SendUnreliable(int client, int channel, const uint8_t* data, int size);
    void Broadcast(int channel, const uint8_t* data, int size, int flags = 0);
    void BroadcastExcept(int except, int channel, const uint8_t* data, int size, int flags = 0);

    // Send (client-side)
    void ClientSend(int channel, const uint8_t* data, int size, int flags = 0);

    // Zero-copy send
    netudp_buffer_t* AcquireBuffer();
    void SendBuffer(int client, int channel, netudp_buffer_t* buf, int flags = 0);

    // Events
    EventQueue<NetworkEvent>& GetEventQueue();
    DelegateHandle OnClientConnected(std::function<void(uint32_t, uint64_t)>);
    DelegateHandle OnClientDisconnected(std::function<void(uint32_t, int)>);
    DelegateHandle OnDataReceived(std::function<void(uint32_t, int, const uint8_t*, int)>);

    // Stats
    NetworkConnectionStats GetConnectionStats(int client) const;
    int GetConnectedClientCount() const;
    void DisconnectClient(int client);

    // Token generation (static, can run on any thread)
    static bool GenerateConnectToken(const NetworkConfig& config,
        const char** servers, int num, uint64_t client_id,
        int expire, uint8_t user_data[256], uint8_t out[2048]);
};

}
```

### REQ-17.5: NetworkConfig

```cpp
namespace uz {

struct NetworkConfig {
    uint64_t    protocol_id       = 0;
    uint8_t     private_key[32]   = {};
    uint16_t    port              = 7777;
    int         max_clients       = 64;
    int         num_channels      = 4;
    bool        compression       = true;
    const char* compression_dict_path = nullptr;
    int         log_level         = 2;  // Info
};

}
```

### REQ-17.6: ECS Components (Optional, Game-Level)

```cpp
namespace uz {

// Marks an entity as network-replicated
struct CNetworkIdentity : public Component {
    uint32_t net_id;             // Unique across all clients
    uint32_t owner_client;       // Which client owns this
    bool     server_authoritative;
    void OnRegister() override;
};

// Syncs transform over network
struct CNetworkTransform : public Component {
    Vec3       server_position;
    Quaternion server_rotation;
    float      interpolation_time;
    void OnBeginPlay() override;
};

}
```

These components are NOT part of netudp — they are UzEngine game-layer code that USES netudp via NetworkingSubsystem.

### REQ-17.7: Build Integration

```cmake
# In UzEngine's CMakeLists.txt:
add_subdirectory(thirdparty/netudp)

target_link_libraries(uzengine PRIVATE netudp::netudp)
target_include_directories(uzengine PRIVATE ${NETUDP_INCLUDE_DIR})
```

Or via CMake FetchContent:
```cmake
FetchContent_Declare(netudp GIT_REPOSITORY https://github.com/uzmigames/netudp GIT_TAG v1.0.0)
FetchContent_MakeAvailable(netudp)
```

## Scenarios

#### Scenario: Subsystem registration and lifecycle
Given UzEngine boot sequence
When SubsystemRegistry::Register(NetworkingSubsystem)
Then networking init runs at PreInit phase (before renderer, before world)
And Tick runs every frame at TickGroup::Update
And Shutdown runs after world shutdown (reverse init order)

#### Scenario: Game server with player spawn
Given NetworkingSubsystem started on port 7777
When a client connects with valid token
Then NetworkEvent::ClientConnected enqueued
And during EventQueue::Flush(), game code receives the event
And game code spawns player entity with CNetworkIdentity

#### Scenario: Zero-copy send pattern in game tick
Given player entity position changed
When game code calls AcquireBuffer() → write position → SendBuffer()
Then no heap allocation occurs (buffer from pre-allocated pool)
And data is queued for Nagle batching
And sent at end of tick during Flush
