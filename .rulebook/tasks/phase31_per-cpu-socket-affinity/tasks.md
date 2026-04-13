## 1. Implementation

- [x] 1.1 Add `socket_set_cpu_affinity(Socket*, int cpu_id)` using `SIO_CPU_AFFINITY` on Windows
- [x] 1.2 Pin primary socket to CPU 0 when num_io_threads > 1
- [x] 1.3 Pin each IO worker socket to CPU N+1 after creation
- [x] 1.4 Linux: no-op (uses thread affinity via sched_setaffinity instead)
- [x] 1.5 Fallback define for SIO_CPU_AFFINITY (Zig CC headers)
- [x] 1.6 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (socket.h, socket.cpp, server.cpp comments)
- [x] 2.2 Write tests covering the new behavior (353/353 pass)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
