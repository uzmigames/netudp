# Proposal: phase31_per-cpu-socket-affinity

## Why

MsQuic creates one socket per CPU core and pins each via `SIO_CPU_AFFINITY`. This tells the NIC RSS (Receive Side Scaling) to deliver packets for that socket exclusively to the bound CPU, eliminating cross-CPU cache invalidation. netudp already creates multiple sockets via SO_REUSEPORT (phase 12), but doesn't bind them to CPUs. Adding affinity completes the per-CPU architecture matching MsQuic's design.

Source: `docs/analysis/udp/04-implementation-comparison.md`

## What Changes

- Windows: apply `SIO_CPU_AFFINITY` on each IO worker socket in server_create
- Map IO worker index to CPU core (simple 1:1 mapping)
- Linux: already has `sched_setaffinity` via `netudp_server_set_thread_affinity` (phase 12)
- Add automatic affinity when num_io_threads > 1 (pin worker N to CPU N)

## Impact

- Affected code: `src/server.cpp` (server_create IO worker setup), `src/socket/socket.cpp`
- Breaking change: NO
- User benefit: Better RSS distribution, reduced cross-CPU cache invalidation
