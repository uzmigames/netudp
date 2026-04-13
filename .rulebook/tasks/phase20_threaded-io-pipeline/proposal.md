# Proposal: phase20_threaded-io-pipeline

## Why

`netudp_server_update()` does everything single-threaded: recv → dispatch → process → send → keepalive → timeout → stats. The user's previous C# servers (ToS-Server-1/5) used dedicated recv and send threads, achieving much higher throughput. Current architecture: one thread calls recv_batch, processes all packets, then loops all connections to send. With 1000+ clients, the send phase alone takes ~7ms (1000 × 7µs sendto) blocking the recv path entirely.

Separating recv, process, and send into a pipeline with dedicated threads will:
- Overlap recv with send (recv thread fills queue while send thread flushes)
- Enable parallel processing across connection groups
- Match the architecture of the user's proven C# servers

## What Changes

- Dedicated recv thread: runs `socket_recv_batch` in a tight loop, pushes packets into a lock-free ring buffer
- Dedicated send thread: drains a send queue, calls `socket_send_batch` (coalesced)
- Game thread (`update()`): drains recv queue → dispatch → process → enqueue sends
- Lock-free SPSC queues between threads (FixedRingBuffer already exists)
- Optional: worker threads per connection group for parallel processing
- Config: `netudp_server_config_t::threading_mode` (single/pipeline/workers)

## Impact

- Affected code: `src/server.cpp` (major refactor of update loop), new thread lifecycle
- Breaking change: NO (single-threaded mode remains default)
- User benefit: 2-4x PPS improvement, overlap recv+send, pipeline parallelism
