# Proposal: phase27_connected-client-socket

## Why

Client sends all packets to a single server address. Using `sendto()` forces a route lookup on every call (~1-3us on Windows). Calling `connect()` on the UDP socket caches the route, then `send()` reuses it — saving 1-3us per packet. Every serious game networking guide recommends this for client sockets.

Source: `docs/analysis/udp/02-send-path-optimization.md`

## What Changes

- After client resolves server address, call `connect()` on the UDP socket
- Replace `socket_send(sock, dest, data, len)` calls in client with `send(sock->handle, data, len, 0)`
- Add `socket_connect()` function to socket API
- Server socket remains unconnected (multiple clients)

## Impact

- Affected code: `src/socket/socket.h`, `src/socket/socket.cpp`, `src/client.cpp`
- Breaking change: NO
- User benefit: -1-3us per client send (route lookup cached)
