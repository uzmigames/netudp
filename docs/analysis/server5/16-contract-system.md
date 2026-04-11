# 16. Contract System & Transpiler

**Files:** `Contract/*.cs`, `Core/Decorators/ContractDecorators.cs`, `Core/Transpilers/`

## Overview

Server5 introduced a **declarative packet definition system** using C# attributes. Packets are defined once and transpiled to both C# (server) and Unreal C++ (client) code automatically.

Server1 had no code generation — every packet was hand-written on both client and server.

## Contract Attribute

```csharp
[Contract("ConnectionAccepted", PacketLayerType.Server, ContractPacketFlags.None, PacketType.ConnectionAccepted)]
public partial struct ConnectionAcceptedPacket {
    [ContractField("uint")]
    public uint Id;

    [ContractField("byte[]", 32)]
    public byte[] ServerPublicKey;

    [ContractField("byte[]", 16)]
    public byte[] Salt;
}
```

## Generated Output

The transpiler generates `INetworkPacket` implementations with `Serialize` and `Size`:

```csharp
// Auto-generated:
public enum ClientPackets : ushort {
    SyncEntity = 0,
    SyncEntityQuantized = 1,
    Pong = 2,
    EnterToWorld = 3,
    RekeyResponse = 4,
}

public enum ServerPackets : ushort {
    Benchmark = 0,
    CreateEntity = 1,
    UpdateEntity = 2,
    RemoveEntity = 3,
    UpdateEntityQuantized = 4,
    RekeyRequest = 5,
    DeltaSync = 6,
}
```

## Contract Flags

```csharp
[Flags]
public enum ContractPacketFlags {
    None            = 0,
    ToEntity        = 1 << 0,  // Send to specific entity
    Queue_ToEntity  = 1 << 1,  // Queue + send to entity
    Reliable_ToEntity = 1 << 2, // Reliable + send to entity
}
```

## Unreal C++ Transpiler

The `UnrealTranspiler` generates Unreal Engine C++ structs and serialization code from the same contract definitions, ensuring client-server packet compatibility.

## Relevance to netudp

netudp is a transport library and won't include a contract system, but the **packet interface pattern** is worth preserving:

```csharp
public interface INetworkPacket {
    int Size { get; }
    void Serialize(ref FlatBuffer buffer);
}
```

In C, this maps to function pointers or a simple serialize callback pattern.
