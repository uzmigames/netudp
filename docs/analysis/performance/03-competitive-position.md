# 03. Competitive Position

Honest assessment of netudp v1.0.0 against the competition.

## PPS Comparison (Visual)

### Windows (single thread, ~1200-byte packets)

```
netudp          ~138K pps    ████████████████████████  (WITH XChaCha20 crypto)
ENet (C, raw)   ~250K pps*   ████████████████████████████████████████  (NO crypto)
ENet (C#)       ~103K pps    ████████████████  (NO crypto)
LiteNetLib(C#)  ~101K pps    ████████████████  (NO crypto)
.NET 8 raw      ~81K pps     █████████████  (NO crypto)
Kcp2k (C#)      ~15K pps     ██  (NO crypto)
GNS (tuned)     ~7K pps†     █  (AES-GCM, rate cap limited)

* ENet raw C estimated 2–4× over C# wrapper
† GNS 8.5 MB/s ÷ 1200 bytes ≈ 7,083 pps
```

**Windows verdict:** We beat every C# library while carrying 948ns of crypto that they don't have. Raw C ENet likely outperforms us purely because it has zero crypto overhead — but it also has zero security.

### Linux (batch I/O projection)

```
netudp (batch)  ~2M pps      ████████████████████████████████████████  (target)
Linux 4-thread  ~1.1M pps    ██████████████████████
Linux 1-thread  ~400K pps    ████████
ENet (C#)       ~184K pps    ███
Unreal          ~83 pps      (invisible at this scale)
```

**Linux verdict:** Our batch I/O target (2M pps) exceeds any C# library by 10×. `recvmmsg`/`sendmmsg` already implemented — the path is clear.

## Feature Matrix

| Feature | netudp | ENet | GNS | yojimbo | LiteNetLib | KCP |
|---------|--------|------|-----|---------|------------|-----|
| Built-in AEAD crypto | Yes | No | Yes | Yes | No | Optional |
| Zero-GC after init | Yes | Yes | No | Partial | No | No |
| Batch I/O (recvmmsg) | Yes | No | No | No | No | No |
| Key rekeying | Yes | No | Yes | Yes | No | No |
| Replay protection | Yes | No | Yes | Yes | No | No |
| DDoS escalation | Yes | No | Partial | No | No | No |
| Bandwidth control | Yes | No | Yes | No | No | No |
| Connect tokens | Yes | No | No | Yes | No | No |
| Pure C API | Yes | Yes | No | No | No | Yes |
| CMake + Zig CC | Yes | No | No | No | No | No |
| Frame coalescing | **No** | No | Yes | No | Partial | No |
| Property delta | **No** | No | N/A | No | No | No |

## Where We Win

1. **Security + performance combined** — only C library with AEAD crypto, replay protection, DDoS mitigation, AND competitive PPS
2. **Zero-GC** — no heap allocations after init; real-time safe
3. **Batch I/O** — `recvmmsg`/`sendmmsg` implemented; no other game UDP lib does this
4. **Cross-platform build** — CMake + Zig CC for single-command cross-compilation
5. **Feature completeness** — bandwidth control, connect tokens, rekeying, all in one library

## Where We Lose

1. **Frame coalescing missing** — each message is 1 UDP packet; massive overhead for small messages (see [06-frame-coalescing.md](06-frame-coalescing.md))
2. **No property delta system** — application must manage state replication manually
3. **No io_uring** — missing the 3–10× Linux performance opportunity
4. **Single-threaded** — no SO_REUSEPORT multi-thread support yet
5. **XChaCha20 vs AES-NI** — 2–3× slower crypto than AES-GCM on CPUs with AES-NI

## Critical Gap: Frame Coalescing

The single biggest missed optimization. Current state:

```
5 small messages (20 bytes each):

  WITHOUT coalescing: 5 packets × 57 bytes = 285 bytes on wire
    - 5 syscalls (36µs), 5 encryptions (4.7µs)
    - 65% overhead

  WITH coalescing: 1 packet × 153 bytes = 153 bytes on wire
    - 1 syscall (7.2µs), 1 encryption (948ns)
    - 35% overhead
```

This is not a nice-to-have — for an MMORPG server, it's the difference between "works" and "impossible on one core."

See [06-frame-coalescing.md](06-frame-coalescing.md) for full analysis.

## vs Unreal Engine Specifically

Unreal's NetDriver operates at a completely different level:

```
Unreal NetDriver total PPS:     ~83 pps    █
netudp capacity:             138,000 pps    ████████████████████████████████████████████████
```

This comparison is misleading because:
- Unreal sends **one packet per client per tick** containing hundreds of compressed property deltas
- Each Unreal packet is semantically rich (actor state, RPCs, property replication)
- netudp sends raw messages — the application does its own state management

The fair comparison: Unreal packs ~50 property updates into 1 packet per tick. netudp currently sends each as a separate packet. **This is exactly why we need frame coalescing** — to match what Unreal does architecturally.
