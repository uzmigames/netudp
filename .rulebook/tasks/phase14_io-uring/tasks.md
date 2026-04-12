## 1. Implementation

- [ ] 1.1 Add CMake detection for `liburing` and kernel version (5.7+ required)
- [ ] 1.2 Add `NETUDP_HAVE_IO_URING` compile flag in CMakeLists.txt
- [ ] 1.3 Implement `socket_create_uring()` with SQ/CQ ring setup (64 entries)
- [ ] 1.4 Implement `socket_recv_uring()` using `IORING_OP_RECVMSG`
- [ ] 1.5 Implement `socket_send_uring()` using `IORING_OP_SENDMSG`
- [ ] 1.6 Implement `socket_recv_batch_uring()` with multi-shot receive
- [ ] 1.7 Probe for `IORING_OP_RECV_ZC` (kernel 6.0+) and use when available
- [ ] 1.8 Wire io_uring backend into server/client via socket abstraction layer
- [ ] 1.9 Fallback: if io_uring init fails, fall back to recvmmsg/loop transparently
- [ ] 1.10 Benchmark: io_uring vs recvmmsg vs loop on Linux
- [ ] 1.11 Build and verify on both Linux and Windows (Windows must compile without io_uring)

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
