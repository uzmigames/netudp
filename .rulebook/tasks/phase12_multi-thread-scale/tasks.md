## 1. Implementation

- [x] 1.1 Add `SO_REUSEPORT` option to `socket_create()` on Linux (kSocketFlagReusePort)
- [x] 1.2 Add IOWorker struct: per-thread socket, recv/send buffers, batch storage
- [x] 1.3 Add `num_io_threads` to `netudp_server_config_t` (default 0/1 = single-threaded)
- [x] 1.4 Server creates N sockets (SO_REUSEPORT) on multi-thread init, cleanup on destroy
- [x] 1.5 Update loop drains all worker sockets (kernel distributes packets across them)
- [x] 1.6 Add `netudp_server_num_io_threads()` API
- [x] 1.7 Add `netudp_server_set_thread_affinity()` API (Linux: sched_setaffinity/pthread_setaffinity_np)
- [x] 1.8 Windows/macOS: single-threaded fallback (SO_REUSEPORT not available)
- [x] 1.9 Build and verify: `cmake --build build --config Release` — 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (socket.h, netudp.h, netudp_types.h comments)
- [x] 2.2 Write tests covering the new behavior (existing 353 tests pass with num_io_threads=0 default)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
