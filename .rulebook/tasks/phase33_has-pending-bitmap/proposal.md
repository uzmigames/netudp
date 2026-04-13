# Proposal: phase33_has-pending-bitmap

## Why

`ChannelScheduler::next_channel()` calls `has_pending()` on every channel for every connection on every coalescing iteration. With 5,000 connections × 2 channels × ~3 calls per packet = **27.9M calls** to `chan::has_pending` in the 5K player benchmark (3.5s total). Most calls return false (empty queue). A per-connection pending bitmap (`uint8_t`, 1 bit per channel) would let the scheduler check one byte instead of calling N functions.

## What Changes

- Add `uint8_t pending_mask` to Connection struct
- Set bit on `channel.queue_send()`, clear on `channel.dequeue_send()` when queue empties
- `ChannelScheduler::next_channel()` checks pending_mask first — returns -1 immediately if zero
- Find highest-priority pending channel via bit scan of pending_mask

## Impact

- Affected code: `src/channel/channel.h`, `src/connection/connection.h`, `src/server.cpp`
- Breaking change: NO
- User benefit: ~3.5s saved in 5K player bench (27.9M → ~5K mask checks)
