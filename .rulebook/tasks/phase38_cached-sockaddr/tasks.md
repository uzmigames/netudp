## 1. Implementation

- [ ] 1.1 Add `sockaddr_storage cached_sa` + `int cached_sa_len` to Connection struct
- [ ] 1.2 Populate cached_sa in `server_handle_connection_request` after setting conn.address
- [ ] 1.3 Add `socket_send_raw(Socket*, const sockaddr*, int sa_len, const void*, int)` overload
- [ ] 1.4 `server_send_packet` uses `socket_send_raw` with `conn.cached_sa` instead of address_to_sockaddr
- [ ] 1.5 Keepalive send path also uses cached_sa
- [ ] 1.6 Benchmark before/after
- [ ] 1.7 Build and verify: tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
