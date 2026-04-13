# Proposal: phase16_rio-backend

## Why

Windows `sendto`/`WSASendTo` costs ~7.2µs per packet due to kernel transitions, route lookups, and WFP processing on every call. Registered I/O (RIO, Windows 8+) eliminates per-operation kernel transitions by using pre-registered buffers and shared-memory completion queues — the same architectural pattern as Linux io_uring. Published benchmarks show RIO achieving 450K–1M PPS vs 120K for sendto (4–8x improvement). This is the single highest-ROI optimization for Windows.

Source: `docs/analysis/performance/07-optimization-roadmap.md`, ServerFramework RIO benchmarks

## What Changes

- New `socket_rio.h` / `socket_rio.cpp`: RIO socket backend mirroring `socket_uring.h`/`.cpp` architecture
- `RioSocket` struct: wraps RIO request queue, completion queue, and pre-registered buffer pool
- `rio_socket_create()`: obtains RIO function table via `WSAIoctl(SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER)`, creates polled CQ + RQ, registers recv/send buffer pools
- `rio_recv_batch()`: pre-posts `RIOReceive` operations, polls CQ via `RIODequeueCompletion` (zero syscall)
- `rio_send_batch()`: submits `RIOSend` operations with pre-converted addresses from registered buffers
- Graceful fallback: if RIO init fails (pre-Win8, driver issue), transparently uses WSASendTo loop
- CMake option: `NETUDP_ENABLE_RIO` (default OFF, opt-in)
- Wire RIO backend into server via socket abstraction layer

## Impact

- Affected specs: socket layer (Windows-only enhancement)
- Affected code: new `src/socket/socket_rio.h`, `src/socket/socket_rio.cpp`, `CMakeLists.txt`, `src/server.cpp` (socket selection)
- Breaking change: NO (opt-in, WSASendTo fallback unchanged)
- User benefit: 4–8x Windows PPS (138K → 500K–1M), closing gap with Linux
