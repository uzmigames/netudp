## 1. Implementation

- [ ] 1.1 Add `socket_set_cpu_affinity(Socket*, int cpu_id)` using `SIO_CPU_AFFINITY` on Windows
- [ ] 1.2 Apply affinity to each IO worker socket during server_create (worker N → CPU N)
- [ ] 1.3 Linux: auto-call `sched_setaffinity` for worker threads when pipeline starts
- [ ] 1.4 Add NETUDP_ZONE profiling for affinity setup
- [ ] 1.5 Config option: `auto_cpu_affinity` (default true when num_io_threads > 1)
- [ ] 1.6 Build and verify on Windows

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
