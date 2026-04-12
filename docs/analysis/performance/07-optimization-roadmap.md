# 07. Optimization Roadmap

Phases ordered by ROI. Recommended execution: **G → F → E → A → B → D → C**.

---

## Phase G — Frame Coalescing (HIGHEST PRIORITY)

**Target:** 5× message throughput, 46% bandwidth reduction, zero protocol changes.

Pack multiple frames into a single UDP packet up to MTU, encrypt once, send once.

**Implementation:**
```
1. Rewrite server_send_pending() to accumulate frames in payload buffer
2. Flush on: MTU full, no more pending messages, or flush API call
3. AckFields written once per packet (not per frame)
4. Track frames_packed count for stats
5. Client-side already handles multi-frame — no change needed
```

**Expected gain:**
- Messages/s: **4.8×** (5 msgs/pkt average)
- Syscalls: **5× reduction**
- Crypto ops: **5× reduction**
- Bandwidth: **46% reduction** on small messages

**Risk:** Very low. Wire format already supports it. Receive parser already iterates frames.

**Estimated effort:** 1–2 days.

---

## Phase F — Profiling Overhead Audit

**Target:** Verify `NETUDP_ZONE` cost is negligible, add compile-time disable.

Each zone call does a lock-free `rdtsc` + atomic add. Estimated ~5–15ns per zone.

```
33 zones × 10ns × 2M pps = 660 ms/s per core = 6.6% CPU overhead
```

**Implementation:**
```
1. Benchmark PPS with NETUDP_PROFILING=0 vs =1
2. If overhead > 3%: verify NETUDP_ZONE is no-op when disabled
3. Add cmake -DNETUDP_PROFILING=OFF build option
4. Document overhead in README
```

**Expected gain:** +3–6% PPS when profiling is disabled.
**Risk:** None.

---

## Phase E — Connection Fast Path

**Target:** conn::reset < 1µs (currently 7.8µs).

Move zero-fill from acquire-time (hot path) to release-time (cold path).

**Implementation:**
```
1. Pool::release() → memset to zero → mark free
2. Pool::acquire() → skip zeroing (already clean)
3. Benchmark connect/disconnect cycle time
```

**Expected gain:** conn::reset off hot path → <200ns acquire.
**Risk:** Very low. Release path is cold.

---

## Phase A — Multi-Thread Scale (Linux)

**Target:** 4× PPS on Linux multi-core (400K → 1.6M pps single-socket, 2M+ with batch).

**Implementation:**
```
1. Add SO_REUSEPORT socket mode to socket_create()
2. Worker thread pool: one socket per thread
3. Lock-free dispatch: each thread owns its connections (5-tuple hash routing)
4. CPU affinity API: netudp_server_set_affinity(thread_id, cpu_id)
5. NUMA-aware pool allocation (Linux: numa_alloc_onnode)
```

**Expected gain:** 3–4× PPS on Linux multi-core.
**Risk:** Medium. Connection routing must be consistent. 5-tuple hash for dispatch.

---

## Phase B — Windows Batch Send

**Target:** 2–4× Windows PPS (138K → 300K).

Windows has no `sendmmsg` but has scatter-gather via `WSASend` with multiple `WSABUF`.

**Implementation:**
```
1. Send-side ring buffer: FixedRingBuffer<SocketPacket, 64>
2. Flush call: WSASend with WSABUF array (coalesced at socket level)
3. Explicit netudp_server_flush() for deterministic latency
4. Measure: WSASend array vs sendto loop
```

**Expected gain:** 2–3× Windows PPS.
**Risk:** Low. Adds latency jitter (configurable `flush_interval_us`).

---

## Phase D — io_uring (Linux)

**Target:** 3–10× Linux PPS (2M → 7M+ pps).

io_uring removes context switch overhead. `recvmsg`/`sendmsg` via io_uring achieves ~7M pps.

**Implementation:**
```
1. io_uring socket backend: socket_create_uring(), socket_recv_uring(), socket_send_uring()
2. Feature detect: IORING_FEAT_FAST_POLL (kernel 5.7+)
3. SQ/CQ ring sizes matching kSocketBatchMax (64)
4. Zero-copy receive: IORING_OP_RECV_ZC (kernel 6.0+)
5. Compile guard: #ifdef NETUDP_HAVE_IO_URING
```

**Expected gain:** 7M+ pps.
**Risk:** Linux 5.7+ kernel required. Fallback to recvmmsg/loop on older kernels.

---

## Phase C — AES-256-GCM Fast Path

**Target:** 2× crypto throughput on CPUs with AES-NI (948ns → ~400ns per packet).

**Implementation:**
```
1. Runtime detection: CPUID → NETUDP_HAVE_AES_NI
2. Add AES-GCM backend (libsodium or hand-rolled with _mm_aesenc_si128)
3. Dispatch: AES-GCM if AES-NI available, else XChaCha20
4. New file: src/crypto/aead_aesni.cpp
5. Opt-in config flag (XChaCha20 remains default for nonce-misuse resistance)
```

**Expected gain:** 948ns → ~400ns per packet.
**Risk:** Medium. AES-GCM fails catastrophically on nonce reuse. Must keep XChaCha20 as default.

**Note:** Only worth implementing AFTER coalescing + io_uring, when crypto becomes a larger fraction of total cost.

---

## Priority Matrix

| Phase | ROI | Effort | Risk | Linux | Windows | Messages/s |
|-------|-----|--------|------|-------|---------|-----------|
| **G** Frame coalescing | Max | Low | Very Low | **5×** | **5×** | **5×** |
| **F** Profiling audit | Low | Very Low | None | +3–6% | +3–6% | Baseline |
| **E** Conn fast path | Low | Very Low | Very Low | Latency | Latency | 0% |
| **A** Multi-thread | High | Medium | Medium | **4×** | Limited | **4×** |
| **B** Windows batch | Medium | Medium | Low | None | **2–3×** | **2–3×** |
| **D** io_uring | High | High | Low | **3–5×** | None | **3–5×** |
| **C** AES-GCM | Medium | High | Medium | **2× crypto** | **2× crypto** | ~6% total |

## Cumulative Effect (Linux, single core)

```
Current:                     ~138K msgs/s
+ Frame coalescing (5×):     ~690K msgs/s
+ Profiling off (+5%):       ~725K msgs/s
+ Multi-thread 4× (4 cores): ~2.9M msgs/s
+ io_uring (3×):             ~8.7M msgs/s (theoretical ceiling)
```

## Milestone Targets

| Milestone | Metric | Platform |
|-----------|--------|----------|
| M0 (current) | 138K pps, 948ns crypto | Windows single, v1.0.0 |
| M1 (coalescing) | 600K+ msgs/s | All platforms |
| M2 (profiling clean) | <3% overhead verified | All |
| M3 (conn fast path) | conn::reset < 1µs | All |
| M4 (Linux multi-thread) | 1.5M+ pps | Linux |
| M5 (Windows batch) | 300K+ pps | Windows |
| M6 (io_uring) | 5M+ pps | Linux 5.7+ |
| M7 (AES fast path) | 400ns/pkt crypto | AES-NI CPUs |
