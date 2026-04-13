# Proposal: phase38_cached-sockaddr

## Why

`address_to_sockaddr()` does `memset(&ss, 0, 128)` + field-by-field copy on every send. ENet only zeroes 16 bytes (IPv4 sockaddr_in). With 65K sends/s this is 65K × 128 = 8.3MB/s of needless zeroing. Caching the pre-converted sockaddr in the Connection struct eliminates this entirely — convert once on connect, reuse for all sends.

## What Changes

- Add `struct sockaddr_storage cached_sockaddr` + `int cached_sockaddr_len` to Connection
- Populate on connect after address is set
- `server_send_packet` uses cached sockaddr instead of calling address_to_sockaddr per send
- `socket_send` gets a new overload accepting pre-converted sockaddr

## Impact

- Affected code: `src/connection/connection.h`, `src/server.cpp`, `src/socket/socket.cpp`
- Breaking change: NO
- User benefit: 2-5% CPU reduction (eliminate 128-byte memset per send)
