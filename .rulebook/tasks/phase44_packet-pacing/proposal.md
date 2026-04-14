# Proposal: phase44_packet-pacing

## Why

Today `server_send_pending` flushes ALL pending packets for ALL clients in a single burst within the game tick. With 1000 clients x 2 packets = 2000 packets emitted in <1ms, followed by 15ms of silence (at 60Hz tick). This causes jitter on the client side — packets arrive in a burst then nothing, making interpolation uneven and movement appear jerky. Real-time servers (Valve Source, Overwatch) pace sends evenly across the tick interval. Spreading 2000 packets over 16ms means each client receives data at consistent intervals, enabling smooth interpolation.

## What Changes

- **Send scheduler**: divide active clients into N slices (default 4). Each slice is flushed at a different sub-tick offset within the frame.
- **Sub-tick timer**: after `server_update`, the send scheduler fires at `0ms, 4ms, 8ms, 12ms` (for 60Hz/4 slices) using high-resolution timer
- **Client slice assignment**: round-robin by slot index. Slice 0 = slots 0-249, slice 1 = 250-499, etc.
- **Configurable**: `netudp_server_config_t::pacing_slices` (0 = disabled/burst mode, default 4)
- **Pipeline mode integration**: in pipeline mode, the send thread paces from the send queue naturally (drain N items, sleep, drain N more)
- **No API change for application**: pacing is transparent — application calls `server_update` once per tick as before

## Impact

- Affected code: `src/server.cpp` (send loop restructure), pipeline send thread
- Dependencies: none (independent of phases 40-43)
- Breaking change: NO (transparent optimization, opt-out via `pacing_slices = 0`)
- User benefit: reduced client-side jitter by 60-80%. Smoother interpolation. No application code changes needed.
