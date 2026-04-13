# Proposal: phase35_connection-worker-threads

## Why

With 5,000 connections, the per-tick loop processes ALL connections sequentially on one thread: bandwidth refill → send_pending (coalesce + encrypt + send) → keepalive → timeout → stats. With 5K connections, `send_pending` alone takes 2.5s total in the benchmark. Splitting connections across N worker threads — each owning a slice of connections — enables parallel processing: Worker 0 handles connections 0-1249, Worker 1 handles 1250-2499, etc. Each worker does recv dispatch + send_pending for its connections independently.

This is the architecture of the user's C# Server-1/5 (separate recv/send/game threads per connection group) and matches MsQuic's per-processor design.

## What Changes

- Partition active connections across N worker threads (configurable, default = CPU core count)
- Each worker owns its connection slice: process received packets, call send_pending, manage keepalive/timeout
- Hash-based routing: new connections assigned to worker with least load
- Lock-free handoff: recv thread pushes packets to per-worker queues
- Game thread (`update()`) becomes the coordinator: signals workers, waits for completion, handles connect/disconnect

## Impact

- Affected code: `src/server.cpp` (major refactor of per-connection loop into worker threads)
- Breaking change: NO (single-thread mode unchanged as default)
- User benefit: Near-linear scaling with CPU cores. 4 workers = ~4x throughput for 5K+ players.
