# 05. Gap Analysis

What's missing to hit production targets.

## Goal 1: 2M PPS (Linux production)

| Gap | Current | Target | Status | Fix |
|-----|---------|--------|--------|-----|
| Linux batch recv | `recvmmsg` 64 pkt/syscall | Done | Done | — |
| Linux batch send | `sendmmsg` 64 pkt/syscall | Done | Done | — |
| Frame coalescing | 1 msg/packet | ~5 msgs/packet | **Missing** | Phase G |
| Multi-thread | Single | SO_REUSEPORT × 4 | **Missing** | Phase A |
| CPU affinity | None | NUMA-local pinning | **Missing** | Phase A |
| Zero-copy path | Partial | netudp_buffer API | Partial | Phase B |
| io_uring | Not implemented | 7M+ pps | **Missing** | Phase D |

## Goal 2: <1ms LAN Latency (P99)

| Gap | Current | Target | Notes |
|-----|---------|--------|-------|
| End-to-end measurement | Not measured | <200µs P99 | Need E2E benchmark |
| conn::reset | 7.8µs | <1µs | Pre-zero in pool release |
| Nagle timer jitter | 0–nagle_ms | Configurable | Already has NO_DELAY flag |
| Poll/wake latency | Unknown | <100µs | Need polling benchmark |
| Frame coalescing latency | N/A | <1ms flush interval | Must be configurable |

## Goal 3: 100K Concurrent Connections

| Gap | Current | Target | Notes |
|-----|---------|--------|-------|
| Pool capacity | Configurable | 100K slots | Memory: 100K × sizeof(Connection) |
| FixedHashMap capacity | N slots | 131,072 (next 2^17) | Must be power-of-2 |
| conn::init | 379µs | <10µs | One-time per conn, pool amortizes |
| Memory footprint | ~64 bytes/conn | ~64 bytes/conn | Already compact |
| Bandwidth tracking | Per-connection | Per-connection | Already implemented |
| DDoS at 100K scale | Rate limiter | L1–L3 escalation | Already implemented |

## Goal 4: MMORPG Server Viability

| Gap | Current | Target | Priority |
|-----|---------|--------|----------|
| **Frame coalescing** | 1 msg = 1 UDP packet | Multiple msgs/packet | **Critical** |
| Property delta encoding | None | VarInt delta compression | High |
| State snapshot/dirty flags | None | Per-entity dirty tracking | High |
| Entity replication | None (raw messages only) | Automatic property sync | Medium |
| Area of interest | None | Spatial partitioning | Medium |
| Position quantization | None | 2–4 bytes per axis | Medium |
| Rotation compression | None | 1 byte (256 values) | Low |

**Frame coalescing is the single highest-impact missing feature** — it affects PPS, bandwidth, crypto overhead, and syscall budget simultaneously. Without it, an MMORPG server sending 100K msgs/s would consume 720ms/s in syscalls alone.

## Impact Summary

| Optimization | Messages/s gain | Syscall reduction | Crypto reduction | Bandwidth saving |
|-------------|----------------|-------------------|------------------|-----------------|
| Frame coalescing (5 msgs/pkt) | **4.8×** | **5×** | **5×** | **46%** |
| Multi-thread (4 cores) | **4×** | 1× per thread | 1× per thread | 0% |
| io_uring | **3–5×** | Amortized | 0% | 0% |
| AES-GCM (with AES-NI) | 1× | 0% | **2×** | 0% |
| conn::reset pre-zero | 0% (latency only) | 0% | 0% | 0% |

**Recommended execution order for MMORPG goal:**
1. Frame coalescing (Phase G) — highest ROI, unblocks everything
2. Multi-thread (Phase A) — scales with cores
3. Property delta system — reduces per-message size
4. io_uring (Phase D) — further PPS boost on Linux
