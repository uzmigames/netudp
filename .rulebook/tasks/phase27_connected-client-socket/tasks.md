## 1. Implementation

- [x] 1.1 Add `socket_connect(Socket*, const netudp_address_t*)` to socket API
- [x] 1.2 Add `socket_send_connected(Socket*, const void*, int)` using `send()` instead of `sendto()`
- [x] 1.3 Call `socket_connect()` in client after resolving server address (before SENDING_REQUEST)
- [x] 1.4 Replace all 4 `socket_send()` calls in client.cpp with `socket_send_connected()`
- [x] 1.5 Remove unused `dest` variables (3 occurrences)
- [x] 1.6 Add NETUDP_ZONE profiling: sock::connect, sock::send_conn
- [x] 1.7 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (socket.h, client.cpp comments)
- [x] 2.2 Write tests covering the new behavior (353/353 pass)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
