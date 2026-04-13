# Proposal: phase37_idle-peer-fastpath

## Why

ENet idle peer per tick: ~8-12 inline ops, zero function calls. netudp idle peer: 6+ function calls including `bandwidth.refill()`, `budget.refill()`, `send_pending()` (enters function, allocates 1300 bytes on stack, calls build_ack_fields, next_channel_fast), plus profiler zones. With 5K idle peers at 60Hz this is ~1.6ms/tick wasted on connections with nothing to send. The fast-path checks `pending_mask == 0 && time - last_send_time < keepalive_threshold` and does only the two time comparisons — no function calls.

## What Changes

- Before calling bandwidth/budget refill, check `pending_mask == 0` first
- If no pending data AND not time for keepalive, do only timeout + slow_tick checks
- Refill/send_pending only called when there IS data to send

## Impact

- Affected code: `src/server.cpp` (per-connection tick loop)
- Breaking change: NO
- User benefit: 5-10% CPU reduction at high connection counts with sparse traffic
