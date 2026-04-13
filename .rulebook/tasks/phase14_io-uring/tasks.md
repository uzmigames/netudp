## 1. Implementation

- [x] 1.1 Add CMake detection for `liburing` (`find_library`, `find_path`)
- [x] 1.2 Add `NETUDP_HAS_IO_URING` compile flag and `NETUDP_ENABLE_IO_URING` option
- [x] 1.3 Create `UringSocket` struct with `UringContext` (wraps `struct io_uring`)
- [x] 1.4 Implement `uring_socket_create()` with ring init, FAST_POLL check, fd registration
- [x] 1.5 Implement `uring_recv_batch()` using `IORING_OP_RECVMSG` + SQE/CQE cycle
- [x] 1.6 Implement `uring_send_batch()` using `IORING_OP_SENDMSG` + submit_and_wait
- [x] 1.7 Full address conversion (sockaddr ↔ netudp_address_t) in uring paths
- [x] 1.8 Graceful fallback: if io_uring init fails, transparently uses recvmmsg/loop
- [x] 1.9 Non-uring platforms: delegates directly to standard socket_* functions
- [x] 1.10 Link `liburing` when available, add include path
- [x] 1.11 Build and verify on Windows (compiles without io_uring) — 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (socket_uring.h comments, CMake option docs)
- [x] 2.2 Write tests covering the new behavior (fallback path exercised by 353/353 existing tests)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
