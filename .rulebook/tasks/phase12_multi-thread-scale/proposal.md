# Proposal: phase12_multi-thread-scale

## Why

Single-threaded Linux tops out at ~400K pps (kernel socket ceiling). Game servers targeting 1,000+ concurrent players at 20-60 Hz need 1.5M+ pps. `SO_REUSEPORT` with 4 worker threads, each owning its socket and connections, gives 4x PPS scaling with no lock contention. Cloudflare measured 1.1M pps with 4 threads on commodity Xeon hardware.

Source: `docs/analysis/performance/07-optimization-roadmap.md` (Phase A)

## What Changes

- Add `SO_REUSEPORT` flag to `socket_create()` on Linux
- Server worker thread pool: one socket per thread, each thread owns a subset of connections
- Lock-free connection routing via 5-tuple hash (same client always hits same thread)
- CPU affinity API: `netudp_server_set_affinity(thread_id, cpu_id)`
- NUMA-aware pool allocation on Linux (`numa_alloc_onnode`)

## Impact

- Affected specs: server architecture (new threading model)
- Affected code: `src/socket/socket.h`, `src/socket/socket.cpp`, `src/server.cpp`, `include/netudp/netudp.h`
- Breaking change: NO (opt-in via server config, single-thread remains default)
- User benefit: 3-4x PPS on Linux multi-core, enabling 5,000+ player worlds
