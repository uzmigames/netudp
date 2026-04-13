# ENet & LiteNetLib Deep Analysis — Performance Gap Investigation

**Date:** 2026-04-13
**Purpose:** Identify exactly why ENet achieves 184K PPS (Linux) / 103K (Windows) vs netudp's 101K, and what we can do to close the gap.

## Key Finding

The gap is NOT one big thing — it's the accumulation of many small overheads that ENet simply doesn't have. ENet is a minimal protocol. Each of netudp's features adds 2-15% overhead, and they compound.

## Root Causes (ranked by impact)

### 1. Profiling Overhead (10-15% impact) — HIGHEST PRIORITY

`NETUDP_ZONE` is default ON. Each zone entry does 2× `QueryPerformanceCounter` (~25ns each) + atomic operations. `send_pending` alone hits 5+ zones. With 5K peers at 60Hz = **1.8M+ QPC calls/s** just for per-peer processing.

**Fix:** Benchmark with `-DNETUDP_DISABLE_PROFILING=ON`. Keep profiling for dev, compile out for production.

### 2. Per-Packet Address Hashing (5-10% impact)

Every received packet does TWO FNV-1a hashes of 20 bytes:
1. `rate_limiter.allow()` — hash AddressKey (20 bytes)
2. `address_to_slot.find()` — hash netudp_address_t (20 bytes)

ENet: ZERO hash computation. Uses `peerID` from packet header as direct array index: `peers[peerID]`. One array dereference.

**Fix:** Embed connection slot index in packet header. Peer lookup becomes `connections[slot_id]` + address validation. Hash map fallback only for connection requests.

### 3. Per-Idle-Peer Tick Cost (5-10% impact)

ENet idle peer per tick: ~8-12 inline operations, zero function calls.

netudp idle peer per tick:
- `bandwidth.refill()` — function call + timestamp math + NETUDP_ZONE
- `budget.refill()` — function call + multiply + NETUDP_ZONE
- `send_pending()` — enters function, allocates 1300 bytes on stack, calls `build_ack_fields`, calls `next_channel_fast`, exits
- Keepalive check: time comparison
- Slow tick check: time comparison

**Fix:** Fast-path for idle peers: if `pending_mask == 0 && time - last_send_time < 1.0`, only do the two time comparisons. No function calls, no bandwidth refill (nothing to send).

### 4. sockaddr_storage Zeroing (2-5% impact)

`address_to_sockaddr()` does `memset(&ss, 0, sizeof(sockaddr_storage))` = **128 bytes zeroed** per send.

ENet: `memset(&sin, 0, sizeof(sockaddr_in))` = **16 bytes** (IPv4 only).

**Fix:** Cache pre-converted sockaddr in Connection struct. Update only on connect. Eliminate per-send memset+conversion.

### 5. Rate Limiter for Known Peers (2-3% impact)

Every incoming packet from a connected peer goes through `rate_limiter.allow()` — hash computation + hash map probe.

ENet: No per-packet rate limiting.

**Fix:** After `address_to_slot` lookup succeeds (known peer), bypass rate limiter. Only rate-limit unknown/unauthenticated packets.

### 6. sim_enabled Branch (<1% impact)

`if (server->sim_enabled)` checked on every packet. Always false in production.

**Fix:** Already branch-predicted away. Negligible.

## What ENet Does NOT Have (each adds CPU cost)

| Feature | netudp | ENet | Cost |
|---------|--------|------|------|
| AEAD Encryption | XChaCha20 | None | ~1.2µs/pkt |
| Per-IP rate limiting | Token bucket | None | Hash + probe/pkt |
| DDoS escalation | 5 levels | None | update() per tick |
| Replay protection | Sequence window | Implicit | ~20ns/pkt |
| Bandwidth token bucket | Per-connection | Simple throttle | refill() per tick |
| QueuedBits budget | Per-connection | None | refill() per tick |
| Congestion AIMD | Full AIMD | Packet throttle | evaluate() per slow tick |
| Connection stats | 30+ fields | Basic counters | update_throughput() |
| Profiling zones | 40+ zones (default ON) | None | 2× QPC per zone |
| Key rekeying | Epoch-based | None | check per send |
| Frame coalescing scheduler | Priority + weight | Queue drain | next_channel_fast() |
| Network simulator | Built-in | None | Branch per packet |

## Optimization Plan (prioritized)

| # | Fix | Impact | Effort | Phase |
|---|-----|--------|--------|-------|
| 1 | Benchmark with profiling OFF | 10-15% | 1 line | Immediate |
| 2 | Embed peerID in packet header | 5-10% | Medium | phase36 |
| 3 | Fast-path for idle peers | 5-10% | Low | phase37 |
| 4 | Cache sockaddr per connection | 2-5% | Low | phase38 |
| 5 | Bypass rate limiter for known peers | 2-3% | Low | phase39 |

Combined projected improvement: **25-45%** → netudp 101K → ~130-145K PPS (matching/exceeding ENet C# 103K Windows).

## Sources

- ENet source: github.com/lsalzman/enet (protocol.c, unix.c, host.c, peer.c)
- LiteNetLib source: github.com/RevenantX/LiteNetLib
- NetworkBenchmarkDotNet: github.com/JohannesDeml/NetworkBenchmarkDotNet (v0.8.2)
- netudp profiling: `netudp_profiling_get_zones()` output from 5K player benchmark
