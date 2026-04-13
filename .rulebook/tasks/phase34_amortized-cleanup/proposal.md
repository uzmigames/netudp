# Proposal: phase34_amortized-cleanup

## Why

`frag::cleanup()` runs on every active connection every tick — 8.8M calls in the 5K player benchmark (2.5s total). Fragment timeout cleanup only needs to run every ~1s (fragment timeout is 5s), not every tick. Similarly, `congestion.evaluate()` runs every tick but only does real work when enough packets have accumulated. `stats.update_throughput()` runs every tick but resets counters only once per second. Amortizing these to run periodically instead of per-tick eliminates millions of empty function calls.

## What Changes

- `frag::cleanup()`: only call if `time - last_frag_cleanup > 1.0` per connection
- `congestion.evaluate()`: only call if `total_packets >= threshold` (already checks internally, but the function call overhead is the issue)
- `stats.update_throughput()`: already amortized to 1s internally, but the function call still costs ~20ns × 5K = 100us/tick
- Add `next_cleanup_time` per connection to skip cleanup on most ticks

## Impact

- Affected code: `src/server.cpp` (per-tick connection loop)
- Breaking change: NO
- User benefit: ~2.5s saved in 5K bench (8.8M → ~5K frag::cleanup calls)
