## 1. C++ RAII Wrapper (P8-A) — spec 16
- [ ] 1.1 Create sdk/cpp/include/netudp.hpp: single header, includes <netudp/netudp.h>, namespace netudp {}
- [ ] 1.2 Implement netudp::Server: constructor(address, config, time) calls netudp_server_create, destructor calls netudp_server_destroy, move semantics (move ctor + move assign, copy deleted), start/stop/update, send(client, channel, span<const uint8_t>, flags), broadcast/broadcast_except, flush
- [ ] 1.3 Implement netudp::Server::receive(): callback version (zero-alloc: void receive(int client, int max, function<void(Message&&)>)), convenience vector version (std::vector<Message> receive(int client, int max=64))
- [ ] 1.4 Implement netudp::Server callbacks: on_connect(function<void(int, uint64_t, span<const uint8_t>)>), on_disconnect(function<void(int, int)>) — stored as std::function, called during update()
- [ ] 1.5 Implement netudp::Client: constructor/destructor, move semantics, copy deleted (both ctor and operator=), connect(span<uint8_t,2048>), update, disconnect, state, connected, send, flush, receive (callback + vector versions), stats
- [ ] 1.6 Implement netudp::Message: wraps netudp_message_t*, destructor calls netudp_message_release, move semantics, data() returns span<const uint8_t>, size/channel/client/sequence/receive_time accessors
- [ ] 1.7 Implement netudp::BufferWriter: fluent API — constructor acquires from pool via Server, destructor releases if not sent, u8/u16/u32/u64/f32/f64/varint/bytes/string methods return BufferWriter&, send(client, channel, flags) and broadcast(channel, flags) consume the buffer
- [ ] 1.8 Implement netudp::BufferReader: construct from Message or span, u8/u16/u32/u64/f32/f64/varint/bytes/string methods, position/remaining/has_data
- [ ] 1.9 Implement netudp::ConnectToken: static generate() → optional<ConnectToken>, wraps netudp_generate_connect_token
- [ ] 1.10 Tests: test_cpp_wrapper.cpp — Server RAII lifecycle (no leaks), move semantics (moved-from handle == nullptr), BufferWriter fluent chain + send, BufferReader round-trip, ConnectToken generate + use

## 2. UzEngine Integration (P8-E) — spec 17
- [ ] 2.1 Create sdk/uzengine/networking_subsystem.h/.cpp: NetworkingSubsystem : ISubsystem, GetName()="Networking", GetInitPhase()=PreInit, GetTickGroup()=Update
- [ ] 2.2 Implement Init(): netudp_init(), configure allocator routing (uz_malloc/uz_free), log SIMD level via UZ_LOG
- [ ] 2.3 Implement Tick(dt): netudp_server_update(), poll events into EventQueue<NetworkEvent>, flush events at end of tick, UZ_PROFILE_SCOPE
- [ ] 2.4 Implement server API: StartServer(config), StopServer, Send/SendReliable/SendUnreliable/Broadcast/BroadcastExcept, AcquireBuffer/SendBuffer, DisconnectClient, GetConnectionStats
- [ ] 2.5 Implement client API: ConnectToServer(address, token), DisconnectFromServer, ClientSend, IsConnected
- [ ] 2.6 Implement events: EventQueue<NetworkEvent> with types ClientConnected/ClientDisconnected/DataReceived, delegate handles OnClientConnected/OnClientDisconnected/OnDataReceived
- [ ] 2.7 Implement NetworkConfig struct: protocol_id, private_key[32], port, max_clients, num_channels, compression flag, dict path, log_level
- [ ] 2.8 Add CMake integration: add_subdirectory(thirdparty/netudp) or FetchContent pattern
- [ ] 2.9 Tests: test_uzengine.cpp — subsystem lifecycle (init → tick → shutdown), event dispatch, zero-copy send via AcquireBuffer

## 3. Unreal Engine 5 Plugin (P8-B) — spec 18
- [ ] 3.1 Create sdk/unreal/NetudpPlugin/ directory structure: .uplugin, Source/NetudpPlugin/ with Build.cs, Public/ (NetudpSubsystem.h, NetudpTypes.h, NetudpBlueprintLibrary.h), Private/ (NetudpSubsystem.cpp, NetudpModule.cpp, NetudpBlueprintLibrary.cpp)
- [ ] 3.2 Create NetudpPlugin.Build.cs: PublicDependencyModuleNames (Core, CoreUObject, Engine), link pre-built netudp static lib per platform (ThirdParty/netudp/lib/{win-x64,linux-x64,mac-arm64})
- [ ] 3.3 Implement UNetudpSubsystem (UGameInstanceSubsystem): Initialize() registers FTSTicker, Deinitialize() removes ticker + destroys server/client, ShouldCreateSubsystem() returns true
- [ ] 3.4 Implement server API: StartServer(Port, MaxClients) UFUNCTION BlueprintCallable, StopServer, Send(ClientIndex, Channel, TArray<uint8>, bReliable), Broadcast, DisconnectClient
- [ ] 3.5 Implement client API: ConnectToServer(Address, TArray<uint8> Token), DisconnectFromServer, GetClientState, ClientSend
- [ ] 3.6 Implement dynamic delegates: FOnClientConnected(int32 ClientIndex, int64 ClientId), FOnClientDisconnected(int32 ClientIndex, int32 Reason), FOnDataReceived(int32 ClientIndex, int32 Channel, TArray<uint8> Data, int64 Sequence) — UPROPERTY BlueprintAssignable
- [ ] 3.7 Implement memory routing: allocate_function → FMemory::Malloc, free_function → FMemory::Free
- [ ] 3.8 Implement stats: GetPing(ClientIndex), GetConnectedClientCount() as BlueprintCallable
- [ ] 3.9 Cross-compile platform libraries: build netudp for win-x64, linux-x64, mac-arm64, place in ThirdParty/netudp/lib/

## 4. Unity C# Bindings (P8-C) — spec 19
- [ ] 4.1 Create sdk/unity/com.uzmigames.netudp/ package structure: package.json, Runtime/ with .asmdef, Plugins/ with pre-built native libs (.dll/.so/.dylib)
- [ ] 4.2 Create Netudp.cs: static P/Invoke bindings for ALL public C API functions — [DllImport("netudp")] with correct marshaling (IntPtr for handles, byte[] for data, string for addresses)
- [ ] 4.3 Create NetudpServer.cs (IDisposable): Start(address, port, maxClients, config), Tick() (call from Update()), Send with NativeArray<byte> (zero-copy via GetUnsafeReadOnlyPtr) and byte[] (copy) overloads, Dispose() calls destroy
- [ ] 4.4 Create NetudpClient.cs (IDisposable): ConnectToServer(address, token), Tick(), Send, Disconnect, Dispose
- [ ] 4.5 Implement zero-GC receive: pre-allocate NativeArray<IntPtr> for message pointers, PollMessages(client, Action<int,int,NativeSlice<byte>>) with unsafe pointer access, release messages after callback
- [ ] 4.6 Create NetudpBuffer.cs: write/read helpers wrapping P/Invoke buffer calls
- [ ] 4.7 Implement C# events: Action<int,ulong> OnClientConnected, Action<int,int> OnClientDisconnected, Action<int,int,NativeSlice<byte>> OnDataReceived — fired during Tick from native callbacks
- [ ] 4.8 Create Editor/NetudpSettingsProvider.cs: Inspector UI for config (protocol_id, port, max_clients, compression toggle)

## 5. Godot 4 GDExtension (P8-D) — spec 20
- [ ] 5.1 Create sdk/godot/ structure: netudp.gdextension, SConstruct, src/ (register_types.cpp, netudp_server.h/.cpp, netudp_client.h/.cpp, netudp_types.h, netudp_buffer.h/.cpp), lib/ (pre-built per platform)
- [ ] 5.2 Implement NetudpServerGD (RefCounted, GDCLASS): _bind_methods(), start(address, port, max_clients), stop, update(time), send(client, channel, PackedByteArray, flags), send_reliable, broadcast, flush, disconnect_client, is_client_connected, get_connected_count, get_ping, get_quality
- [ ] 5.3 Implement NetudpClientGD (RefCounted, GDCLASS): connect_to_server(address, PackedByteArray token), update, disconnect, get_state, is_connected, send, get_ping
- [ ] 5.4 Implement Godot signals: ADD_SIGNAL(MethodInfo("client_connected", ...)), "client_disconnected", "data_received" — emit during update() call, data as PackedByteArray (copy from native buffer)
- [ ] 5.5 Implement static generate_connect_token(): exposed to GDScript, wraps netudp_generate_connect_token, returns PackedByteArray(2048)
- [ ] 5.6 Add note in docs: client_id (uint64) appears as signed int in GDScript — values above INT64_MAX are negative
- [ ] 5.7 Tests: GDScript test scene — server start, client connect via token, send/receive PackedByteArray, signals fire correctly

## 6. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 6.1 Update CHANGELOG.md: "Added: C++ RAII wrapper, UzEngine subsystem, Unreal 5 plugin, Unity C# bindings, Godot 4 GDExtension"
- [ ] 6.2 All C++ wrapper tests pass, UzEngine tests pass
- [ ] 6.3 Unreal plugin compiles against UE5.4+ (or verified via manual build)
- [ ] 6.4 Unity package imports without errors in Unity 2022+
- [ ] 6.5 Godot GDExtension loads and signals fire in Godot 4.2+
- [ ] 6.6 Per-engine quick-start guide in sdk/<engine>/README.md
