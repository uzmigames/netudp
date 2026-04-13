# Proposal: phase23_channel-scheduler-bitmap

## Why

`ChannelScheduler::next_channel()` is O(num_channels) per call, called multiple times per coalesced packet send (once per frame packed + 1 final). With 4 channels and 3 frames/packet: 12 iterations per packet × 20K packets/sec = 240K iterations/sec. Also, `has_pending()` recomputes `nagle_ms / 1000.0` floating point division on every call. Replacing with a pending bitmap and precomputed nagle threshold eliminates both.

## What Changes

- Add `uint8_t pending_mask` to Connection (1 bit per channel, up to 8 channels)
- Update pending_mask on `queue_send()` (set bit) and when channel drains empty (clear bit)
- `next_channel()`: scan pending_mask with bit tricks instead of iterating array
- Precompute `nagle_threshold_sec` at channel init (avoid per-call division)
- Replace `has_pending()` time check with integer comparison

## Impact

- Affected code: `src/channel/channel.h` (has_pending, scheduler), `src/server.cpp` (send_pending)
- Breaking change: NO
- User benefit: 3-5% CPU reduction, eliminates repeated FP division
