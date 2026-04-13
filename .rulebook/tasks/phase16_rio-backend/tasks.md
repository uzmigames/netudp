## 1. Implementation

- [ ] 1.1 Add CMake option `NETUDP_ENABLE_RIO` (default OFF) with `NETUDP_HAS_RIO` compile flag
- [ ] 1.2 Create `RioContext` struct: RIO function table, CQ handle, buffer IDs, address buffer pool
- [ ] 1.3 Create `RioSocket` struct mirroring `UringSocket` (base Socket + RioContext pointer)
- [ ] 1.4 Implement `rio_socket_create()`: WSAIoctl to obtain RIO function table, register recv/send buffer pools via `RIORegisterBuffer`, create polled CQ via `RIOCreateCompletionQueue`, create RQ via `RIOCreateRequestQueue`
- [ ] 1.5 Implement recv pre-posting: submit N `RIOReceive` operations at init, re-post after each completion
- [ ] 1.6 Implement `rio_recv_batch()`: poll CQ via `RIODequeueCompletion` (zero syscall), extract packets from registered buffers, convert `SOCKADDR_INET` to `netudp_address_t`
- [ ] 1.7 Implement `rio_send_batch()`: convert addresses to `RIO_BUF_ADDR`, submit `RIOSend` per packet, flush via `RIONotify` or dequeue send completions
- [ ] 1.8 Implement `rio_socket_destroy()`: deregister buffers, close CQ/RQ, free buffer pools
- [ ] 1.9 Graceful fallback: if any RIO init step fails, set `rio = nullptr` and fall through to WSASendTo path
- [ ] 1.10 Non-Windows compile guard: RioSocket delegates to standard socket_* functions
- [ ] 1.11 Build and verify: `cmake --build build --config Release`

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
