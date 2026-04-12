# Proposal: phase11_connection-fast-path

## Why

`conn::reset` takes 7.8us due to zero-filling the Connection struct at acquire time (hot path). For servers with frequent connects/disconnects (MMORPG lobby, matchmaking), this adds latency to the connection establishment critical path. Moving the zero-fill to pool release (cold path) makes acquire near-instant.

Source: `docs/analysis/performance/07-optimization-roadmap.md` (Phase E)

## What Changes

- Move `memset` zero-fill from `Pool::acquire()` to `Pool::release()`
- `Pool::release()` zeroes the slot and marks it free
- `Pool::acquire()` skips zeroing (slot is already clean from prior release)
- Net effect: conn::reset cost moves off the connect hot path

## Impact

- Affected specs: none
- Affected code: `src/core/pool.h`, `src/connection/connection.h`
- Breaking change: NO
- User benefit: conn::reset from 7.8us to <200ns on the connect hot path
