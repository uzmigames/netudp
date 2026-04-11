# 2. Public API (ISteamNetworkingSockets)

## Core Operations

```cpp
// Listen / Connect
HSteamListenSocket CreateListenSocketIP(addr, options);
HSteamNetConnection ConnectByIPAddress(addr, options);
EResult AcceptConnection(hConn);
bool CloseConnection(hConn, reason, debug, linger);

// Send (single + batch)
EResult SendMessageToConnection(hConn, data, size, flags, &msgNum);
void SendMessages(nMessages, messages[], results[]);
EResult FlushMessagesOnConnection(hConn);

// Receive (single connection + poll group)
int ReceiveMessagesOnConnection(hConn, messages[], nMax);
int ReceiveMessagesOnPollGroup(hPollGroup, messages[], nMax);

// Lanes
EResult ConfigureConnectionLanes(hConn, nLanes, priorities[], weights[]);

// Stats
EResult GetConnectionRealTimeStatus(hConn, &status, nLanes, laneStatus[]);
int GetDetailedConnectionStatus(hConn, buf, bufSize);

// Poll groups (receive from any connection)
HSteamNetPollGroup CreatePollGroup();
bool SetConnectionPollGroup(hConn, hPollGroup);
```

## Config Options (Extensive)

100+ configurable options including:
- Send rate limits
- Nagle time
- MTU
- Timeout values
- Encryption mode
- Connection quality thresholds
- Logging levels

## C Flat API

```c
// Every C++ method has a flat C equivalent:
SteamAPI_ISteamNetworkingSockets_SendMessageToConnection(hSockets, hConn, data, size, flags, &msgNum);
```

## Key Pattern: Poll Groups

```cpp
// Create a group, assign connections, receive from any:
HSteamNetPollGroup group = CreatePollGroup();
SetConnectionPollGroup(conn1, group);
SetConnectionPollGroup(conn2, group);

SteamNetworkingMessage_t *msgs[64];
int n = ReceiveMessagesOnPollGroup(group, msgs, 64);
for (int i = 0; i < n; i++) {
    // msgs[i]->m_conn tells which connection
    ProcessMessage(msgs[i]);
    msgs[i]->Release();
}
```

This pattern avoids polling each connection individually — essential for servers with 100+ connections.
