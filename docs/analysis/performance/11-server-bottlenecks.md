# 11. Server Architecture Bottleneck Analysis

## Current Architecture

```
netudp_server_update() — SINGLE THREAD, SEQUENTIAL:

  1. DDoS monitor tick
  2. Recv batch loop (socket_recv_batch → dispatch all packets)
     └─ Per packet: O(N) address scan → decrypt → deliver
  3. Drain IO worker sockets (if multi-socket mode)
  4. Drain network simulator
  5. For EVERY slot (0..max_clients):  ← O(max_clients), not O(active)
     ├─ Bandwidth refill
     ├─ send_pending (coalesce → encrypt → sendto)
     ├─ Keepalive check
     ├─ Timeout check
     ├─ Congestion evaluate
     ├─ Fragment cleanup
     └─ Stats update
```

## Bottleneck Ranking

| # | Issue | CPU Impact | Fix |
|---|-------|-----------|-----|
| 1 | **O(N) address lookup per packet** | 15-25% | Hash map (phase 19) |
| 2 | **Single-threaded recv+send** | 50%+ throughput ceiling | Threaded pipeline (phase 20) |
| 3 | **O(max_clients) per-tick scan** | 8-12% | Active connection list (phase 21) |
| 4 | **O(N) fingerprint scan** | 5-10% | Hash map + LRU (phase 22) |
| 5 | **Channel scheduler O(channels) per frame** | 3-5% | Bitmap (phase 23) |
| 6 | **Broadcast O(max_clients) + N copies** | 1-2% per broadcast | Shared frame (phase 24) |

## Detailed Analysis

### 1. O(N) Address Lookup (phase 19)

```cpp
// server.cpp:357 — called on EVERY incoming data packet
for (int i = 0; i < server->max_clients; ++i) {
    if (server->connections[i].active &&
        netudp_address_equal(&server->connections[i].address, from)) {
        slot = i;
        break;
    }
}
```

With 1000 clients × 10K inbound pps = **10M iterations/sec** in this loop alone.

### 2. Single-Threaded Pipeline (phase 20)

```
Current (serialized):
  [recv 2ms] → [process 3ms] → [send 7ms] = 12ms/tick = 83 ticks/sec

With pipeline (overlapped):
  Thread 1: [recv] [recv] [recv] [recv] ...
  Thread 2:        [proc] [proc] [proc] ...
  Thread 3:               [send] [send] ...
  
  Effective: max(recv, proc, send) = 7ms/tick = 142 ticks/sec (+71%)
```

The C# Server-1/5 used this pattern: separate recv/send/game threads.

### 3. Per-Tick Full Slot Scan (phase 21)

```cpp
// server.cpp:474 — runs every tick
for (int i = 0; i < server->max_clients; ++i) {
    if (!conn.active) continue;  // Skipped, but memory still touched
    // ... bandwidth, send, keepalive, timeout, stats
}
```

10,000 slots × 512 bytes = 5.1 MB scanned per tick. L2 cache is 256KB-1MB.
With 100 active: 99% of iterations are wasted `continue` branches.

### 4. Fingerprint O(N) Scan (phase 22)

```cpp
// server.cpp:776 — on every connection request
for (int i = 0; i < server->fingerprint_count; ++i) {
    // memcmp 8 bytes + address_equal per entry
}
```

1024 entries × 100 connect attempts/sec = 102K iterations/sec.
Plus: no eviction when cache fills → new tokens silently dropped.

### 5. Channel Scheduler Overhead (phase 23)

```cpp
// Called 3x per coalesced packet × 20K packets/sec = 60K calls/sec
static int next_channel(Channel* channels, int num_channels, double now) {
    for (int i = 0; i < num_channels; ++i) {
        if (channels[i].has_pending(now)) { // FP division inside!
```

`has_pending()` computes `nagle_ms / 1000.0` on every call.

### 6. Broadcast Serialization (phase 24)

```cpp
void broadcast(server, channel, data, bytes, flags) {
    for (int i = 0; i < server->max_clients; ++i) {  // O(max_clients)
        if (active) netudp_server_send(...);           // N × memcpy
    }
}
```

1000 players × 50-byte update = 50KB of memcpy for identical data.

## Projected Impact

```
Current (single-threaded, O(N) dispatch):
  PPS ceiling: ~80-90K (Windows)

After phase 19 (O(1) dispatch):          +15-25%  → ~100-115K
After phase 21 (active list):             +8-12%  → ~110-130K
After phase 22 (fingerprint hash):        +5-10%  → ~115-140K
After phase 23 (bitmap scheduler):        +3-5%   → ~120-145K
After phase 20 (threaded pipeline):       +70-100% → ~200-290K

Combined estimate:                        ~250-300K PPS (Windows)
                                          ~800K-1.2M PPS (Linux, recvmmsg + threads)
```

## Execution Order

```
phase 19 → phase 21 → phase 22 → phase 23 → phase 20 → phase 24
  O(1)      active      fingerprint  bitmap    threads    broadcast
  dispatch  list        hash         sched     pipeline   optimize

Rationale:
- 19/21/22/23 are independent, can be done in any order
- Each reduces per-packet CPU, making the threading gain larger
- 20 (threading) should come after per-packet optimizations to maximize ROI
- 24 (broadcast) depends on 21 (active list)
```
