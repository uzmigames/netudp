# Proposal: phase13_windows-batch-send

## Why

Windows has no `sendmmsg` equivalent, so each `sendto` costs ~7.2us. Single-threaded Windows PPS caps at ~138K. Using `WSASend` with scatter-gather `WSABUF` arrays can coalesce multiple UDP datagrams into fewer kernel transitions, targeting 2-3x improvement (300K pps). This makes Windows a viable development and testing platform for high-load scenarios.

Source: `docs/analysis/performance/07-optimization-roadmap.md` (Phase B)

## What Changes

- Add send-side ring buffer to Socket: `FixedRingBuffer<SocketPacket, 64>`
- Flush function: calls `WSASend` with `WSABUF` array (coalesced kernel transition)
- `netudp_server_flush()` explicit flush for deterministic latency control
- Configurable `flush_interval_us` to balance throughput vs latency
- Linux/macOS: no change (already have `sendmmsg`)

## Impact

- Affected specs: socket layer (Windows-only enhancement)
- Affected code: `src/socket/socket.h`, `src/socket/socket.cpp` (Windows section)
- Breaking change: NO
- User benefit: 2-3x Windows PPS (138K to 300K), better dev/test experience
