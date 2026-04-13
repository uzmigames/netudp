## 1. Implementation

- [ ] 1.1 Add `socket_connect(Socket*, const netudp_address_t*)` to socket API
- [ ] 1.2 Add `socket_send_connected(Socket*, const void*, int)` using `send()` instead of `sendto()`
- [ ] 1.3 Call `socket_connect()` in client after resolving server address
- [ ] 1.4 Replace `socket_send()` in client_send_pending with `socket_send_connected()`
- [ ] 1.5 Server path unchanged (unconnected socket for multi-client)
- [ ] 1.6 Add NETUDP_ZONE profiling for connected send path
- [ ] 1.7 Build and verify: tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
