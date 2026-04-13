# Proposal: phase28_pipeline-batch-send

## Why

The pipeline send thread (server.cpp:255) calls `socket_send()` individually for each packet popped from the send queue. This means one syscall per packet — the same bottleneck as single-threaded mode, just on a different thread. The send thread should drain multiple packets and call `socket_send_batch()` once, amortizing the syscall overhead. On Linux this becomes one `sendmmsg()` for up to 64 packets; on Windows, a tight WSASendTo loop with pre-converted addresses.

Source: `docs/analysis/udp/02-send-path-optimization.md`

## What Changes

- pipeline_send_thread: drain up to kSocketBatchMax packets from send_queue into a SocketPacket array
- Call `socket_send_batch()` once per drain cycle (not socket_send per packet)
- Pre-convert addresses once for the batch

## Impact

- Affected code: `src/server.cpp` (pipeline_send_thread)
- Breaking change: NO
- User benefit: ~2x send throughput in pipeline mode (fewer syscalls per cycle)
