# Proposal: SDK Bindings — C++ Wrapper, UzEngine, Unreal, Unity, Godot

## Why
The raw extern "C" API is designed for FFI compatibility, not developer ergonomics. Game developers using C++, C#, or GDScript need idiomatic wrappers that handle RAII lifetime, provide type-safe APIs, and integrate with each engine's memory, event, and build systems. The C++ RAII wrapper is the foundation (used by UzEngine and Unreal), while Unity and Godot get their own bindings. This makes netudp a drop-in replacement for engine-specific networking in each ecosystem.

## What Changes
- C++ header-only wrapper (sdk/cpp/): Server, Client, Message RAII classes, BufferWriter fluent API, BufferReader, ConnectToken helper
- UzEngine integration (sdk/uzengine/): NetworkingSubsystem (ISubsystem), EventQueue dispatch, PoolAllocator routing, ECS component examples
- Unreal Engine 5 plugin (sdk/unreal/): UNetudpSubsystem (UGameInstanceSubsystem), Blueprint API, dynamic delegates, FTSTicker tick, platform libs
- Unity C# bindings (sdk/unity/): P/Invoke bindings, NetudpServer/Client wrappers, NativeArray zero-GC pattern, C# events
- Godot 4 GDExtension (sdk/godot/): NetudpServer/Client RefCounted classes, GDScript signals, PackedByteArray, connect token generation

## Impact
- Affected specs: 16-sdk-cpp-wrapper, 17-sdk-uzengine, 18-sdk-unreal, 19-sdk-unity, 20-sdk-godot
- Affected code: sdk/cpp/, sdk/uzengine/, sdk/unreal/, sdk/unity/, sdk/godot/
- Breaking change: NO (additive — new sdk/ directory)
- User benefit: Idiomatic API for every major game engine
