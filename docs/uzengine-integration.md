# netudp × UzEngine — Integration Architecture

How netudp integrates natively with UzEngine to provide transparent multiplayer networking, similar to how Unreal Engine's NetDriver works.

---

## 1. UzEngine Architecture Summary

| Subsystem | Pattern | Key Types |
|---|---|---|
| **Lifecycle** | `ISubsystem` with `Init/Tick/Shutdown` | `InitPhase::PreInit`, `TickGroup::Update` |
| **Memory** | `PoolAllocator<T>`, `LinearAllocator`, `FrameAllocator` | `Own<T>`, `Ref<T>`, `RefMut<T>` |
| **Containers** | `RingBuffer<T,N>`, `FreeList<T>`, `HandleMap<K,V>`, `FrameArray<T>` | Zero-alloc, no std:: |
| **ECS** | entt-based, generational `Entity` handles | `Component`, `World`, prefix `C` |
| **Events** | `MulticastDelegate<Args...>`, `EventQueue<TEvent>` | Frame-deferred dispatch |
| **Jobs** | `Job`, `MakeJob(lambda)`, `Counter` | 48-byte inline closure |
| **Math** | `Vec2`, `Vec3`, `Vec4`, `Quaternion`, `Mat4` | SIMD (DirectXMath) |
| **Logging** | `UZ_LOG(Category, Level, fmt, ...)` | `UZ_DECLARE_LOG_CATEGORY()` |
| **Namespace** | `uz::` | PascalCase classes, `C` prefix for components |

---

## 2. Integration Model: NetworkingSubsystem

netudp integrates as a first-class `ISubsystem` in the UzEngine subsystem registry.

```
UzEngine Subsystem Registry
  │
  ├── Window           (InitPhase::PreInit)
  ├── Config           (InitPhase::PreInit)
  ├── Networking ◄──── netudp lives here
  │     InitPhase::PreInit
  │     TickGroup::Update
  │     Dependencies: []
  ├── Scripting        (InitPhase::Init)
  ├── Renderer         (InitPhase::Init)
  ├── Physics          (InitPhase::Init)
  └── World            (InitPhase::Start)
```

### 2.1 NetworkingSubsystem

```cpp
// engine/networking/networking_subsystem.h
#pragma once

#include <netudp/netudp.h>
#include "engine/app/subsystem.h"
#include "engine/memory/allocators.h"
#include "engine/memory/ownership.h"
#include "engine/event/event_queue.h"
#include "engine/containers/ring_buffer.h"

UZ_DECLARE_LOG_CATEGORY(Networking);

namespace uz {

// ─── Network Events ────────────────────────────────────────────

struct NetworkEvent {
    enum class Type : uint8_t {
        ClientConnected,
        ClientDisconnected,
        DataReceived,
    };

    Type        type;
    uint32_t    client_index;
    uint64_t    client_id;       // From connect token
    int         channel;
    int         disconnect_reason;
    const uint8_t * data;        // Valid only during event dispatch
    int         data_size;
    uint64_t    sequence;
};

// ─── Network Stats (per-connection) ────────────────────────────

struct NetworkConnectionStats {
    uint32_t ping_ms;
    float    quality;            // 0..1
    float    out_bytes_per_sec;
    float    in_bytes_per_sec;
    uint32_t pending_reliable;
    uint32_t pending_unreliable;
    float    compression_ratio;
};

// ─── Server Configuration ──────────────────────────────────────

struct NetworkConfig {
    uint64_t    protocol_id       = 0;
    uint8_t     private_key[32]   = {};
    uint16_t    port              = 7777;
    int         max_clients       = 64;
    int         num_channels      = 4;
    bool        compression       = true;
    const char* compression_dict  = nullptr;   // Path to netc dict file
    int         log_level         = 2;         // Info
};

// ─── Subsystem ─────────────────────────────────────────────────

class NetworkingSubsystem : public ISubsystem {
public:
    // ISubsystem interface
    const char*              GetName() const override { return "Networking"; }
    InitPhase                GetInitPhase() const override { return InitPhase::PreInit; }
    TickGroup                GetTickGroup() const override { return TickGroup::Update; }
    std::vector<const char*> GetDependencies() const override { return {}; }

    bool Init() override;
    void Shutdown() override;
    void Tick(float deltaTime) override;

    // ── Server lifecycle ──
    bool StartServer(const NetworkConfig& config);
    void StopServer();
    bool IsServerRunning() const;

    // ── Client lifecycle ──
    bool ConnectToServer(const char* address, uint8_t* connect_token);
    void DisconnectFromServer();
    bool IsConnected() const;
    int  GetClientState() const;

    // ── Send (server-side) ──
    void Send(int client_index, int channel, const uint8_t* data, int size, int flags = 0);
    void SendReliable(int client_index, int channel, const uint8_t* data, int size);
    void SendUnreliable(int client_index, int channel, const uint8_t* data, int size);
    void Broadcast(int channel, const uint8_t* data, int size, int flags = 0);
    void BroadcastExcept(int except_client, int channel, const uint8_t* data, int size, int flags = 0);

    // ── Send (client-side) ──
    void ClientSend(int channel, const uint8_t* data, int size, int flags = 0);

    // ── Zero-copy send (acquire buffer from pool → write → send) ──
    netudp_buffer_t* AcquireBuffer();
    void             SendBuffer(int client_index, int channel, netudp_buffer_t* buf, int flags = 0);

    // ── Events (subscribe for frame-paced delivery) ──
    EventQueue<NetworkEvent>& GetEventQueue() { return m_eventQueue; }

    // Subscribe shortcuts
    DelegateHandle OnClientConnected(std::function<void(uint32_t client, uint64_t id)> cb);
    DelegateHandle OnClientDisconnected(std::function<void(uint32_t client, int reason)> cb);
    DelegateHandle OnDataReceived(std::function<void(uint32_t client, int channel,
                                                      const uint8_t* data, int size)> cb);

    // ── Stats ──
    NetworkConnectionStats GetConnectionStats(int client_index) const;
    int                    GetConnectedClientCount() const;
    void                   DisconnectClient(int client_index);

    // ── Connect token generation (utility, can run on any thread) ──
    static bool GenerateConnectToken(
        const NetworkConfig& config,
        const char** server_addresses, int num_servers,
        uint64_t client_id, int expire_seconds,
        uint8_t user_data[256], uint8_t out_token[2048]);

private:
    netudp_server_t*       m_server = nullptr;
    netudp_client_t*       m_client = nullptr;
    EventQueue<NetworkEvent> m_eventQueue;
    NetworkConfig          m_config;
    bool                   m_isServer = false;
    double                 m_time = 0.0;

    // Compression dictionary (loaded from asset)
    netc_dict_t*           m_compressionDict = nullptr;

    void PollServerEvents();
    void PollClientEvents();
};

} // namespace uz
```

### 2.2 Implementation

```cpp
// engine/networking/networking_subsystem.cpp
#include "networking_subsystem.h"
#include "engine/logging/log.h"
#include "engine/profiling/profiler.h"

UZ_DEFINE_LOG_CATEGORY(Networking);

namespace uz {

bool NetworkingSubsystem::Init() {
    int result = netudp_init();
    if (result != NETUDP_OK) {
        UZ_LOG(Networking, Error, "Failed to initialize netudp: %d", result);
        return false;
    }
    UZ_LOG(Networking, Info, "netudp initialized (SIMD: %d)", netudp_simd_level());
    return true;
}

void NetworkingSubsystem::Shutdown() {
    StopServer();
    DisconnectFromServer();
    if (m_compressionDict) netc_dict_free(m_compressionDict);
    netudp_term();
    UZ_LOG(Networking, Info, "netudp shutdown");
}

void NetworkingSubsystem::Tick(float deltaTime) {
    UZ_PROFILE_SCOPE("Networking::Tick");

    m_time += deltaTime;

    if (m_server) {
        netudp_server_update(m_server, m_time);
        PollServerEvents();
    }

    if (m_client) {
        netudp_client_update(m_client, m_time);
        PollClientEvents();
    }

    // Flush deferred events to subscribers
    m_eventQueue.Flush();
}

bool NetworkingSubsystem::StartServer(const NetworkConfig& config) {
    m_config = config;

    // Load compression dictionary if configured
    if (config.compression && config.compression_dict) {
        // Load via VFS...
        // netc_dict_load(blob, size, &m_compressionDict);
    }

    netudp_server_config_t cfg = {};
    netudp_default_server_config(&cfg);
    cfg.protocol_id = config.protocol_id;
    memcpy(cfg.private_key, config.private_key, 32);
    cfg.compression_dict = m_compressionDict;
    cfg.log_level = config.log_level;

    // Route netudp allocations through UzEngine allocator
    cfg.allocator_context = nullptr;  // Could pass PoolAllocator*
    cfg.allocate_function = [](void* ctx, size_t bytes) -> void* {
        return uz_malloc(bytes);  // UzEngine global allocator
    };
    cfg.free_function = [](void* ctx, void* ptr) {
        uz_free(ptr);
    };

    // Connection callbacks
    cfg.callback_context = this;
    cfg.connect_disconnect_callback = [](void* ctx, int client_index, int connected) {
        auto* self = static_cast<NetworkingSubsystem*>(ctx);
        NetworkEvent e;
        e.type = connected ? NetworkEvent::Type::ClientConnected
                           : NetworkEvent::Type::ClientDisconnected;
        e.client_index = client_index;
        self->m_eventQueue.Enqueue(e);
    };

    char addr[64];
    snprintf(addr, sizeof(addr), "0.0.0.0:%d", config.port);

    m_server = netudp_server_create(addr, &cfg, m_time);
    if (!m_server) {
        UZ_LOG(Networking, Error, "Failed to create server on port %d", config.port);
        return false;
    }

    netudp_server_start(m_server, config.max_clients);
    m_isServer = true;

    UZ_LOG(Networking, Info, "Server started on port %d (max %d clients)",
           config.port, config.max_clients);
    return true;
}

void NetworkingSubsystem::PollServerEvents() {
    UZ_PROFILE_SCOPE("Networking::PollServer");

    netudp_message_t* msgs[64];
    for (int i = 0; i < m_config.max_clients; i++) {
        if (!netudp_server_client_connected(m_server, i)) continue;

        int count = netudp_server_receive(m_server, i, msgs, 64);
        for (int j = 0; j < count; j++) {
            NetworkEvent e;
            e.type = NetworkEvent::Type::DataReceived;
            e.client_index = i;
            e.channel = msgs[j]->channel;
            e.data = static_cast<const uint8_t*>(msgs[j]->data);
            e.data_size = msgs[j]->size;
            e.sequence = msgs[j]->message_number;

            m_eventQueue.Enqueue(e);
            netudp_message_release(msgs[j]);
        }
    }
}

// ... Send, Broadcast, Stats implementations (trivial wrappers)

} // namespace uz
```

---

## 3. Game Code Integration

### 3.1 Simple Dedicated Server

```cpp
// game/server_main.cpp
#include "engine/app/application.h"
#include "engine/networking/networking_subsystem.h"

class GameServer : public uz::Application {
    uz::NetworkingSubsystem* m_net = nullptr;

    uz::ApplicationConfig Configure() override {
        uz::ApplicationConfig cfg;
        cfg.headless = true;  // No window/renderer
        cfg.tickRate = 30.0f; // 30 Hz server tick
        return cfg;
    }

    void OnStart() override {
        m_net = GetSubsystem<uz::NetworkingSubsystem>();

        uz::NetworkConfig netConfig;
        netConfig.protocol_id = 0x5546504700000001ULL;  // "UZGAME01"
        netConfig.port = 7777;
        netConfig.max_clients = 64;
        // Load private key from config...

        m_net->StartServer(netConfig);

        // Subscribe to network events
        m_net->OnClientConnected([this](uint32_t client, uint64_t id) {
            UZ_LOG(Game, Info, "Player %llu connected (slot %d)", id, client);
            SpawnPlayerEntity(client, id);
        });

        m_net->OnClientDisconnected([this](uint32_t client, int reason) {
            UZ_LOG(Game, Info, "Player disconnected (slot %d, reason %d)", client, reason);
            DestroyPlayerEntity(client);
        });

        m_net->OnDataReceived([this](uint32_t client, int channel,
                                      const uint8_t* data, int size) {
            HandleGamePacket(client, channel, data, size);
        });
    }

    void OnUpdate(float dt) override {
        // Game logic runs here
        // Network events already dispatched by NetworkingSubsystem::Tick()

        // Send world state to all connected clients
        BroadcastWorldState();
    }

    void HandleGamePacket(uint32_t client, int channel,
                          const uint8_t* data, int size) {
        if (size < 1) return;
        uint8_t packetType = data[0];

        switch (packetType) {
            case 0x01: HandleMovePacket(client, data + 1, size - 1); break;
            case 0x02: HandleActionPacket(client, data + 1, size - 1); break;
            case 0x03: HandleChatPacket(client, data + 1, size - 1); break;
        }
    }

    void HandleMovePacket(uint32_t client, const uint8_t* data, int size) {
        // Deserialize position from data
        // Update entity in ECS
        auto& world = GetWorld();
        auto entity = m_playerEntities[client];
        if (world.IsValid(entity)) {
            auto& transform = world.GetComponent<uz::CTransform>(entity);
            // Read position from packet buffer...
        }
    }

    void BroadcastWorldState() {
        // Acquire buffer from pool (zero-alloc)
        auto* buf = m_net->AcquireBuffer();

        // Write entity states
        netudp_buffer_write_u8(buf, 0x10);  // ENTITY_UPDATE packet type
        // ... write entity positions, rotations, states

        // Broadcast to all on unreliable channel
        m_net->Broadcast(1, /* channel: unreliable */
                         buf->data, buf->position, NETUDP_SEND_UNRELIABLE);
    }
};

UZ_APPLICATION_MAIN(GameServer)
```

### 3.2 Client-Side (with Rendering)

```cpp
// game/client_main.cpp
class GameClient : public uz::Application {
    uz::NetworkingSubsystem* m_net = nullptr;

    void OnStart() override {
        m_net = GetSubsystem<uz::NetworkingSubsystem>();

        // Connect token obtained from web backend (HTTPS)
        uint8_t connectToken[2048];
        FetchConnectTokenFromBackend(connectToken);

        m_net->ConnectToServer("127.0.0.1:7777", connectToken);

        m_net->OnDataReceived([this](uint32_t client, int channel,
                                      const uint8_t* data, int size) {
            HandleServerPacket(data, size);
        });
    }

    void OnUpdate(float dt) override {
        // Send input to server (unreliable, high frequency)
        SendInputPacket();

        // Apply received state (interpolation, prediction)
        InterpolateEntities(dt);
    }

    void SendInputPacket() {
        auto* buf = m_net->AcquireBuffer();
        netudp_buffer_write_u8(buf, 0x01);  // MOVE packet

        auto& cam = GetCamera();
        netudp_buffer_write_f32(buf, cam.position.x);
        netudp_buffer_write_f32(buf, cam.position.y);
        netudp_buffer_write_f32(buf, cam.position.z);

        m_net->ClientSend(1, buf->data, buf->position, NETUDP_SEND_UNRELIABLE);
    }
};

UZ_APPLICATION_MAIN(GameClient)
```

---

## 4. ECS Network Components (Optional)

For entity-level networking (replication), optional components can be added:

```cpp
// engine/networking/components/network_component.h
namespace uz {

struct CNetworkIdentity : public Component {
    uint32_t net_id;        // Unique network ID across all clients
    uint32_t owner_client;  // Which client owns this entity
    bool     is_server_authoritative;

    void OnRegister() override { /* assign net_id */ }
};

struct CNetworkTransform : public Component {
    // Server: send position/rotation to clients
    // Client: interpolate between received states
    Vec3       server_position;
    Quaternion server_rotation;
    float      interpolation_time;

    void OnBeginPlay() override { /* start interpolating */ }
};

} // namespace uz
```

These are **game-level** components that USE netudp — they are NOT part of the netudp library itself.

---

## 5. Memory Flow (Zero-GC End-to-End)

```
UzEngine Frame
  │
  │ NetworkingSubsystem::Tick(dt)
  │   │
  │   ├── netudp_server_update(time)        ← zero-alloc (netudp internal pools)
  │   │     ├── recv from socket            ← buffer from recv_pool
  │   │     ├── decrypt (AEAD)              ← in-place, no alloc
  │   │     ├── decompress (netc)           ← pre-allocated arena
  │   │     └── deliver to message_pool     ← pre-allocated message slot
  │   │
  │   ├── PollServerEvents()                ← zero-alloc
  │   │     ├── netudp_server_receive()     ← returns pointers into message_pool
  │   │     ├── Enqueue NetworkEvent        ← EventQueue uses FrameAllocator
  │   │     └── netudp_message_release()    ← returns to message_pool (O(1))
  │   │
  │   └── m_eventQueue.Flush()              ← dispatch to subscribers (zero-alloc)
  │
  │ Game Logic (event handlers)
  │   ├── HandleMovePacket()                ← reads from event data pointer
  │   ├── BroadcastWorldState()
  │   │     ├── AcquireBuffer()             ← from netudp packet_pool (O(1))
  │   │     ├── Write data                  ← into pool buffer (zero-copy)
  │   │     ├── Send()                      ← queued for Nagle batching
  │   │     └── (buffer auto-released after send)
  │   └── ...
  │
  │ End of frame: FrameAllocator::Reset()   ← reclaims all frame temporaries
```

**Zero allocations from socket to game logic to send.** Every step uses pre-allocated pools.

---

## 6. Comparison with Unreal Engine's Approach

| Aspect | Unreal Engine | UzEngine + netudp |
|---|---|---|
| Net driver | `UIpNetDriver` (C++, monolithic) | `NetworkingSubsystem` (C++, modular) |
| Protocol | Custom reliable UDP (NetConnection) | netudp (connect tokens, AEAD, channels) |
| Replication | Property replication via `UPROPERTY(Replicated)` | Manual via `CNetworkTransform` / events |
| RPC | `UFUNCTION(Server/Client/NetMulticast)` | Packet handlers via `OnDataReceived` |
| Serialization | `FArchive` bit stream | `netudp_buffer_t` read/write helpers |
| Encryption | AES + HMAC (optional) | ChaCha20-Poly1305 (always on) |
| Compression | Oodle (proprietary) | netc (open-source, beats Oodle UDP) |
| Memory | UE allocator (`FMemory`) | UzEngine `PoolAllocator` + netudp pools |
| Auth | Custom / Steam / EOS | Connect tokens (netcode.io standard) |
| GC | UE GC for UObjects | **Zero-GC** end-to-end |

**Key advantage:** UzEngine + netudp is fully zero-GC from socket to game logic. Unreal's UObject-based networking has GC pauses for replicated actors.

---

## 7. Future: Replication Framework

A future UzEngine module (NOT part of netudp) could provide Unreal-like automatic replication:

```cpp
// Future: engine/networking/replication/replicated_component.h (NOT in netudp)
namespace uz {

class CReplicated : public Component {
    // Tracks dirty properties per client
    // Generates delta packets automatically
    // Sends via NetworkingSubsystem
};

// Usage:
struct CPlayerHealth : public CReplicated {
    REPLICATED_PROPERTY(int, health, 100);
    REPLICATED_PROPERTY(int, max_health, 100);
    // Changes automatically sent to owning client
};

} // namespace uz
```

This sits on TOP of netudp — the transport layer remains agnostic.
