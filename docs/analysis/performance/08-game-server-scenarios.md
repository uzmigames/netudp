# 08. Game Server Scenarios

PPS budgets for real game server workloads and whether netudp can handle them.

## Methodology

```
Total server PPS = players × updates_per_player × tick_rate
Messages/s = Total PPS (1 message = 1 game state update or event)
```

With frame coalescing, messages/s ≠ packets/s — multiple messages fit in one UDP packet.

## Scenario Matrix

### Without Frame Coalescing (current)

Each message = 1 UDP packet = 1 syscall + 1 encrypt.

| Scenario | Players | Updates/player | Hz | Msgs/s | Syscall cost | Verdict |
|----------|---------|---------------|-----|--------|-------------|---------|
| Casual FPS (32p) | 32 | 5 | 64 | 10,240 | 74ms/s | Trivial |
| Competitive FPS (64p, 128Hz) | 64 | 5 | 128 | 40,960 | 295ms/s | OK |
| Battle Royale (100p) | 100 | 5 | 60 | 30,000 | 216ms/s | OK |
| MMO Zone (1,000p, sparse) | 1,000 | 2 | 30 | 60,000 | 432ms/s | Tight |
| MMO Zone (1,000p, dense) | 1,000 | 5 | 20 | 100,000 | **720ms/s** | **Impossible** |
| MMO World (5,000p) | 5,000 | 2 | 20 | 200,000 | **1,440ms/s** | **Impossible** |
| Sim (10K NPCs, S2S) | 10,000 | 2 | 20 | 400,000 | **2,880ms/s** | **Impossible** |

Syscall cost = msgs/s × 7.2µs (Windows sendto).

### With Frame Coalescing (proposed, ~5 msgs/packet)

| Scenario | Msgs/s | Packets/s | Syscall cost | Crypto cost | Total | Verdict |
|----------|--------|-----------|-------------|-------------|-------|---------|
| Casual FPS (32p) | 10,240 | 2,048 | 15ms/s | 2ms/s | 17ms/s | Trivial |
| Competitive FPS (64p) | 40,960 | 8,192 | 59ms/s | 8ms/s | 67ms/s | Easy |
| Battle Royale (100p) | 30,000 | 6,000 | 43ms/s | 6ms/s | 49ms/s | Easy |
| MMO Zone (1,000p sparse) | 60,000 | 12,000 | 86ms/s | 11ms/s | 97ms/s | OK |
| **MMO Zone (1,000p dense)** | **100,000** | **20,000** | **144ms/s** | **19ms/s** | **163ms/s** | **OK** |
| MMO World (5,000p) | 200,000 | 40,000 | 288ms/s | 38ms/s | 326ms/s | Tight |
| Sim (10K NPCs, S2S) | 400,000 | 80,000 | 576ms/s | 76ms/s | 652ms/s | Multi-thread |

**Frame coalescing makes 1,000-player dense MMO zones viable on a single Windows core.**

### With Coalescing + Multi-Thread (4 cores, Linux)

| Scenario | Msgs/s | Pkts/s | Pkts/core | Viable? |
|----------|--------|--------|-----------|---------|
| MMO Zone (1,000p dense) | 100,000 | 20,000 | 5,000 | Trivial |
| MMO World (5,000p) | 200,000 | 40,000 | 10,000 | Easy |
| Sim (10K NPCs, S2S) | 400,000 | 80,000 | 20,000 | OK |
| MMO Mega (20K, spatial AOI) | 400,000* | 80,000 | 20,000 | OK |

*With Area of Interest: 20K players but each only sees ~100 nearby → 20K × 2 × 20Hz = 400K but distributed.

## Bandwidth Budgets

### Per-player bandwidth (downstream, from server)

| Message type | Raw size | With delta encoding | Frequency |
|-------------|---------|-------------------|-----------|
| Position update (3D) | 12 bytes (3× float) | 4–6 bytes (VarInt delta) | 20–60 Hz |
| Rotation | 4 bytes (float) | 1 byte (256 values) | 20–60 Hz |
| Animation state | 2–4 bytes | 1–2 bytes | On change |
| Health/stats | 8 bytes | 2–4 bytes (VarInt) | On change |
| Chat message | ~64 bytes | ~64 bytes (no delta) | Rare |
| Equipment change | ~16 bytes | ~8 bytes | Rare |

### Per-player bandwidth at different tick rates

| Config | Messages/s | Raw bytes/s | With delta | With coalescing overhead |
|--------|-----------|-------------|-----------|------------------------|
| 20 Hz, 2 updates | 40 | 480 B/s | 240 B/s | ~400 B/s |
| 30 Hz, 3 updates | 90 | 1,080 B/s | 540 B/s | ~800 B/s |
| 60 Hz, 5 updates | 300 | 3,600 B/s | 1,800 B/s | ~2,400 B/s |

### Total server bandwidth

| Scenario | Players | Per-player | Total downstream | Upstream (inputs) |
|----------|---------|-----------|-----------------|-------------------|
| FPS 32p 64Hz | 32 | 2.4 KB/s | 77 KB/s | ~10 KB/s |
| BR 100p 60Hz | 100 | 2.4 KB/s | 240 KB/s | ~30 KB/s |
| MMO 1,000p 20Hz | 1,000 | 0.4 KB/s | 400 KB/s | ~60 KB/s |
| MMO 5,000p 20Hz (AOI) | 5,000 | 0.4 KB/s | 2 MB/s | ~300 KB/s |

All well within 1 Gbps network capacity.

## Comparison with Unreal Engine

```
Unreal UT2004 (real match):
  Total: ~83 pps, ~20 Kbps per additional client
  Method: 1 packet/client/tick with compressed property deltas
  Each packet: ~50 property updates coalesced

netudp v1.0.0 (current):
  Total: 138,000 pps capacity, but 1 msg/packet
  Method: Each property update = separate UDP packet
  Problem: 1,660× more PPS capacity than Unreal, but wastes it

netudp v1.1 (with coalescing):
  Method: ~5 property updates per UDP packet
  Result: Matches Unreal's architectural efficiency
  Advantage: Crypto + zero-GC + batch I/O that Unreal doesn't have
```

## Conclusions

1. **Without coalescing:** netudp can handle up to ~500-player servers comfortably on Windows single thread. Dense MMO zones (1,000+) are physically impossible.

2. **With coalescing:** 1,000-player dense MMO zones become viable on a single core. 5,000-player worlds need multi-thread.

3. **With coalescing + multi-thread + Linux:** Anything up to 20,000-player worlds with spatial AOI is achievable without kernel bypass.

4. **Frame coalescing is the prerequisite for every MMORPG scenario.** Multi-thread and io_uring multiply PPS capacity, but without coalescing the per-message overhead makes the multiplication pointless.
