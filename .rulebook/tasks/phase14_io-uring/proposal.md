# Proposal: phase14_io-uring

## Why

`recvmmsg`/`sendmmsg` still require context switches per syscall. io_uring removes this overhead by submitting and completing I/O via shared ring buffers in userspace. Benchmarks show ~7M pps with io_uring vs ~2M with recvmmsg — a 3-5x improvement. This is the highest PPS ceiling achievable without full kernel bypass (DPDK).

Source: `docs/analysis/performance/07-optimization-roadmap.md` (Phase D)

## What Changes

- New io_uring socket backend: `socket_create_uring()`, `socket_recv_uring()`, `socket_send_uring()`
- Runtime feature detection: `IORING_FEAT_FAST_POLL` (kernel 5.7+)
- SQ/CQ ring sizes matching `kSocketBatchMax` (64)
- Zero-copy receive: `IORING_OP_RECV_ZC` (kernel 6.0+) when available
- Compile guard: `#ifdef NETUDP_HAVE_IO_URING`
- Fallback: `recvmmsg`/`sendmmsg` on older kernels, loop on Windows/macOS

## Impact

- Affected specs: socket layer (Linux-only enhancement)
- Affected code: `src/socket/socket.h`, new `src/socket/socket_uring.cpp`, `CMakeLists.txt`
- Breaking change: NO (opt-in, automatic fallback)
- User benefit: 5M+ pps on Linux 5.7+, approaching kernel bypass performance
