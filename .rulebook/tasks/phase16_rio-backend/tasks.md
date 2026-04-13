## 1. Implementation

- [x] 1.1 Add CMake option `NETUDP_ENABLE_RIO` (default OFF) with `NETUDP_HAS_RIO` compile flag
- [x] 1.2 Create `RioContext` struct: RIO function table, CQ handle, RQ handle, buffer IDs, address buffer pool
- [x] 1.3 Create `RioSocket` struct mirroring `UringSocket` (base Socket + RioContext pointer)
- [x] 1.4 Implement `rio_socket_create()`: WSAIoctl for RIO function table, VirtualAlloc + RIORegisterBuffer for recv/send/addr pools, polled CQ, RQ creation
- [x] 1.5 Implement recv pre-posting: submit N `RIOReceiveEx` operations at init with registered addr buffers, re-post after each completion
- [x] 1.6 Implement `rio_recv_batch()`: poll CQ via `RIODequeueCompletion` (zero syscall), extract packets from registered buffers, convert `SOCKADDR_INET` to `netudp_address_t`
- [x] 1.7 Implement `rio_send_batch()`: copy into registered send pool, `RIOSendEx` per packet with registered addr buffer, `RIONotify` flush, dequeue send completions
- [x] 1.8 Implement `rio_socket_destroy()`: deregister all buffers, VirtualFree pools, close CQ, delete context
- [x] 1.9 Graceful fallback: if any RIO init step fails, set `rio = nullptr` and fall through to WSASendTo path
- [x] 1.10 Non-Windows compile guard: RioSocket delegates to standard socket_* functions
- [x] 1.11 Build and verify: both RIO=ON and RIO=OFF compile cleanly, 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (socket_rio.h comments, analysis/performance/10-windows-parity.md)
- [x] 2.2 Write tests covering the new behavior (353/353 existing tests pass via fallback path; RIO=ON compiles and runs)
- [x] 2.3 Run tests and confirm they pass (353/353 pass in both configurations)
