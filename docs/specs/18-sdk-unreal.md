# Spec 18 — Unreal Engine 5 Plugin (sdk/unreal)

## Requirements

### REQ-18.1: Plugin Structure

```
sdk/unreal/NetudpPlugin/
├── NetudpPlugin.uplugin
├── Source/
│   ├── NetudpPlugin/
│   │   ├── NetudpPlugin.Build.cs
│   │   ├── Public/
│   │   │   ├── NetudpSubsystem.h
│   │   │   ├── NetudpTypes.h
│   │   │   └── NetudpBlueprintLibrary.h
│   │   └── Private/
│   │       ├── NetudpSubsystem.cpp
│   │       ├── NetudpModule.cpp
│   │       └── NetudpBlueprintLibrary.cpp
│   └── ThirdParty/
│       └── netudp/           # Pre-built libs per platform
│           ├── include/
│           ├── lib/win-x64/
│           ├── lib/linux-x64/
│           └── lib/mac-arm64/
```

### REQ-18.2: UNetudpSubsystem

```cpp
UCLASS()
class NETUDPPLUGIN_API UNetudpSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

    // Tick via FTSTicker (registered in Initialize, removed in Deinitialize).
    // FTSTicker avoids multiple-inheritance issues with UObject and is the
    // standard approach for UGameInstanceSubsystem tick in UE5.
    void Tick(float DeltaTime);
    FTSTicker::FDelegateHandle TickHandle;

    // Server
    UFUNCTION(BlueprintCallable, Category = "Netudp")
    bool StartServer(int32 Port, int32 MaxClients);

    UFUNCTION(BlueprintCallable, Category = "Netudp")
    void StopServer();

    UFUNCTION(BlueprintCallable, Category = "Netudp")
    void Send(int32 ClientIndex, int32 Channel, const TArray<uint8>& Data, bool bReliable = false);

    UFUNCTION(BlueprintCallable, Category = "Netudp")
    void Broadcast(int32 Channel, const TArray<uint8>& Data, bool bReliable = false);

    UFUNCTION(BlueprintCallable, Category = "Netudp")
    void DisconnectClient(int32 ClientIndex);

    // Client
    UFUNCTION(BlueprintCallable, Category = "Netudp")
    bool ConnectToServer(const FString& Address, const TArray<uint8>& ConnectToken);

    UFUNCTION(BlueprintCallable, Category = "Netudp")
    void DisconnectFromServer();

    UFUNCTION(BlueprintCallable, Category = "Netudp")
    int32 GetClientState() const;

    UFUNCTION(BlueprintCallable, Category = "Netudp")
    void ClientSend(int32 Channel, const TArray<uint8>& Data, bool bReliable = false);

    // Stats
    UFUNCTION(BlueprintCallable, Category = "Netudp")
    int32 GetPing(int32 ClientIndex) const;

    UFUNCTION(BlueprintCallable, Category = "Netudp")
    int32 GetConnectedClientCount() const;

    // Events (C++ delegates)
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnClientConnected, int32, ClientIndex, int64, ClientId);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnClientDisconnected, int32, ClientIndex, int32, Reason);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnDataReceived, int32, ClientIndex, int32, Channel,
        const TArray<uint8>&, Data, int64, Sequence);

    UPROPERTY(BlueprintAssignable, Category = "Netudp")
    FOnClientConnected OnClientConnected;

    UPROPERTY(BlueprintAssignable, Category = "Netudp")
    FOnClientDisconnected OnClientDisconnected;

    UPROPERTY(BlueprintAssignable, Category = "Netudp")
    FOnDataReceived OnDataReceived;

private:
    netudp_server_t* Server = nullptr;
    netudp_client_t* Client = nullptr;
    double Time = 0.0;
};
```

### REQ-18.3: Memory Routing

SHALL route netudp allocations through Unreal's `FMemory`:

```cpp
cfg.allocate_function = [](void*, size_t bytes) -> void* { return FMemory::Malloc(bytes); };
cfg.free_function = [](void*, void* ptr) { FMemory::Free(ptr); };
```

### REQ-18.4: Blueprint Support

Basic operations (start, stop, send, receive, stats) SHALL be callable from Blueprints.
Data is passed as `TArray<uint8>` in Blueprint API (zero-copy not possible from BP).
For C++ users, raw `netudp_server_t*` handle is accessible via `GetRawHandle()`.

### REQ-18.5: Platform Libraries

SHALL ship pre-built static libraries for:
- `win-x64` (MSVC)
- `linux-x64` (GCC)
- `mac-arm64` (Clang)

Build.cs SHALL link the correct library per platform.

## Scenarios

#### Scenario: Blueprint dedicated server
Given an Unreal project with NetudpPlugin enabled
When Blueprint calls StartServer(7777, 64)
Then server starts listening on port 7777
And OnClientConnected fires when a client connects

#### Scenario: C++ game server in Unreal
Given UNetudpSubsystem accessible via GetGameInstance()->GetSubsystem<UNetudpSubsystem>()
When C++ code calls subsystem->Send(0, 1, data, true)
Then reliable message sent to client 0 on channel 1
