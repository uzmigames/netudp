## 1. C++ RAII Wrapper (P8-A) — spec 16
- [x] 1.1 Create sdk/cpp/netudp.hpp: single header, includes netudp/netudp.h, namespace netudp {}
- [x] 1.2 Implement netudp::Server: RAII (create/destroy), move semantics, start/stop/update, send/broadcast/flush
- [x] 1.3 Implement netudp::Server::receive(): callback version (zero-alloc), receive_all batch
- [x] 1.4 Implement netudp::Server callbacks: on_connect/on_disconnect (stored as std::function)
- [x] 1.5 Implement netudp::Client: RAII, move semantics, connect/update/disconnect/state/connected/send/flush/receive
- [x] 1.6 Implement netudp::Message: wraps netudp_message_t*, RAII release, move-only, data/size/channel/client/bytes/as<T>
- [x] 1.7 Implement netudp::BufferWriter: fluent API — u8/u16/u32/u64/f32/f64/varint/bytes/string, chainable
- [x] 1.8 Implement netudp::BufferReader: u8/u16/u32/u64/f32/f64/varint
- [x] 1.9 Implement netudp::generate_connect_token: wraps C API with std::vector<std::string>
- [x] 1.10 ServerConfig/ClientConfig builders: fluent API for protocol_id/private_key/channels/num_io_threads/crypto_mode
- [x] 1.11 Init guard: RAII netudp_init()/netudp_term()
- [x] 1.12 Channel/SendFlags enums wrapping C constants
- [x] 1.13 Example: sdk/cpp/example.cpp — echo server + client
- [x] 1.14 README: sdk/cpp/README.md — API reference with code examples

## 2. UzEngine Integration (P8-E) — spec 17
- [ ] 2.1 Create sdk/uzengine/networking_subsystem.h/.cpp
- [ ] 2.2 Implement Init(), Tick(), Shutdown()
- [ ] 2.3 Implement server/client API
- [ ] 2.4 Implement EventQueue<NetworkEvent>
- [ ] 2.5 Tests

## 3. Unreal Engine 5 Plugin (P8-B) — spec 18
- [ ] 3.1 Create sdk/unreal/NetudpPlugin/ directory structure
- [ ] 3.2 UNetudpSubsystem (UGameInstanceSubsystem)
- [ ] 3.3 Blueprint API, delegates, FTSTicker
- [ ] 3.4 Memory routing, stats
- [ ] 3.5 Cross-compile platform libraries

## 4. Unity C# Bindings (P8-C) — spec 19
- [ ] 4.1 Create sdk/unity/com.uzmigames.netudp/
- [ ] 4.2 P/Invoke bindings
- [ ] 4.3 NetudpServer/Client (IDisposable)
- [ ] 4.4 Zero-GC receive, NativeArray
- [ ] 4.5 C# events, Editor UI

## 5. Godot 4 GDExtension (P8-D) — spec 20
- [ ] 5.1 Create sdk/godot/ structure
- [ ] 5.2 NetudpServerGD/ClientGD (RefCounted, GDCLASS)
- [ ] 5.3 Signals, PackedByteArray
- [ ] 5.4 GDScript test scene

## 6. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 6.1 Update or create documentation covering the implementation
- [ ] 6.2 Write tests covering the new behavior
- [ ] 6.3 Run tests and confirm they pass
