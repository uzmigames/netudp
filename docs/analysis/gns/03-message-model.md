# 3. Message Model & Send Flags

## Send Flags (Bitfield)

```cpp
k_nSteamNetworkingSend_Unreliable       = 0   // Fire-and-forget
k_nSteamNetworkingSend_NoNagle          = 1   // Bypass Nagle delay
k_nSteamNetworkingSend_NoDelay          = 4   // Bypass Nagle + queue immediately
k_nSteamNetworkingSend_Reliable         = 8   // Guaranteed delivery
k_nSteamNetworkingSend_UseCurrentThread = 16  // Send immediately on calling thread
k_nSteamNetworkingSend_AutoRestartBrokenSession = 32  // P2P: auto-reconnect

// Common combinations:
k_nSteamNetworkingSend_UnreliableNoNagle  = 0|1     // Unreliable, skip batching
k_nSteamNetworkingSend_UnreliableNoDelay  = 0|4|1   // Unreliable, immediate
k_nSteamNetworkingSend_ReliableNoNagle    = 8|1     // Reliable, skip batching
```

## Key Design: Reliability is Per-Message, Not Per-Channel

Unlike netcode.io (always unreliable) or typical channel systems (reliable channel vs unreliable channel), GNS allows **each individual message** to choose its delivery semantics via flags.

## Message Size: 512 KB

```cpp
const int k_cbMaxSteamNetworkingSocketsMessageSizeSend = 512 * 1024;
```

GNS handles fragmentation internally. A 512 KB reliable message will be split into MTU-sized segments, encrypted, sent, acknowledged, and reassembled automatically. The application never sees fragments.

## SteamNetworkingMessage_t

```cpp
struct SteamNetworkingMessage_t {
    void *m_pData;                        // Payload pointer
    int m_cbSize;                         // Payload size
    HSteamNetConnection m_conn;           // Which connection
    SteamNetworkingIdentity m_identityPeer; // Who sent it
    int64 m_nConnUserData;                // User data from connection
    SteamNetworkingMicroseconds m_usecTimeReceived;
    int64 m_nMessageNumber;               // Sequence number
    int m_nSendFlags;                     // Only k_nSteamNetworkingSend_Reliable valid on receive
    uint16 m_idxLane;                     // Which lane
    // ... internal fields, release function, etc.
};
```

## Receive API

```cpp
// Receive up to nMaxMessages at once (batch receive)
int ReceiveMessagesOnConnection(HSteamNetConnection hConn,
    SteamNetworkingMessage_t **ppOutMessages, int nMaxMessages);

// Receive from any connection in a poll group
int ReceiveMessagesOnPollGroup(HSteamNetPollGroup hPollGroup,
    SteamNetworkingMessage_t **ppOutMessages, int nMaxMessages);
```

Messages are returned as an array of pointers. Application must call `Release()` on each.

## Batch Send API

```cpp
// Send multiple messages to multiple connections in one call
void SendMessages(int nMessages,
    SteamNetworkingMessage_t *const *pMessages,
    int64 *pOutMessageNumberOrResult);
```
