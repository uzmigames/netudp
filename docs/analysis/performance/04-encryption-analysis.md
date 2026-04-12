# 04. Encryption Analysis

## Algorithm Comparison (modern CPUs with AES-NI)

| Algorithm | Throughput (AES-NI/HW) | Per 1,200B packet | Notes |
|-----------|----------------------|-------------------|-------|
| AES-256-GCM (Intel AES-NI) | 862–3,731 MB/s | 321–1,394 ns | Fastest with HW acceleration |
| ChaCha20-Poly1305 | 467–1,580 MB/s | 759–2,573 ns | No HW required, consistent |
| **XChaCha20-Poly1305 (netudp)** | **~1,270 MB/s** | **~945 ns** | monocypher, measured |

Range reflects varying payload sizes (small = lower, large = higher throughput).

Source: [Ash's Blog: AES-256 Beats ChaCha20 on Every CPU (2025)](https://ashvardanian.com/posts/chacha-vs-aes-2025/)

## Per-CPU Comparison (AES-GCM vs ChaCha20)

| CPU | AES-256-GCM | ChaCha20-Poly1305 | AES speedup |
|-----|-------------|-------------------|-------------|
| AMD Zen 5 | 862–3,731 MB/s | 467–1,580 MB/s | 1.8–2.4× |
| Intel Ice Lake | 577–2,617 MB/s | 396–1,158 MB/s | 1.5–2.3× |
| Apple M2 Pro | 661–3,131 MB/s | 327–1,088 MB/s | 2.0–2.9× |
| ARM Graviton 4 | 563–2,436 MB/s | 280–895 MB/s | 2.0–2.7× |

**Key finding:** AES-256-GCM beats ChaCha20-Poly1305 by up to 3× on ALL modern CPUs with AES-NI. However, this advantage only matters if crypto is the bottleneck — and currently it is not (see below).

## Is Crypto the Bottleneck?

```
Per-packet cost breakdown (current, single thread):

  sock::send           7,231 ns  ████████████████████████████████████████  87.4%
  crypto::encrypt        948 ns  █████  11.5%
  framing + queue         70 ns  ░  0.8%
  crypto overhead        ~25 ns  ░  0.3%
```

**No.** Crypto is 11.5% of per-packet cost. The syscall dominates at 87.4%.

Even with frame coalescing (5 msgs/packet), crypto becomes:
```
  sock::send           7,231 ns  ████████████████████████████████████████  84.7%
  crypto::encrypt        948 ns  █████  11.1%
  5× framing + queue    350 ns  ██  4.1%
```

Still dominated by the syscall. AES-GCM would save ~548 ns per packet (948 → ~400), reducing total per-packet time by 6.4%. Measurable but not transformative.

## When AES-GCM Becomes Worth It

AES-GCM optimization becomes meaningful when:
1. **Frame coalescing is done** — crypto % increases as syscall is amortized
2. **io_uring is done** — syscall cost drops from 7,231ns to ~500ns
3. **Multi-thread** — crypto becomes per-core bounded

With io_uring + coalescing:
```
  io_uring send          ~500 ns  ████████  26%
  crypto::encrypt         948 ns  ████████████████████  50%  ← NOW it matters
  5× framing + queue     350 ns  ███████  18%
  overhead               ~100 ns  ██  5%
```

At that point, AES-GCM (400ns vs 948ns) would give **29% total improvement**.

## Security Trade-off

| Property | XChaCha20-Poly1305 | AES-256-GCM |
|----------|-------------------|-------------|
| Nonce size | 24 bytes | 12 bytes |
| Nonce reuse impact | Reduced (extended nonce space) | **Catastrophic** (key recovery possible) |
| HW acceleration | None needed | Requires AES-NI / ARMv8 CE |
| Side-channel resistance | Constant-time by design | Timing attacks if no HW |
| Nonce space | 2^192 (virtually unlimited) | 2^96 (requires careful management) |

**Recommendation:** Keep XChaCha20 as default. Offer AES-GCM as opt-in for users who control their deployment and want the 2× crypto speedup on AES-NI hardware.

## kcp-go Encryption Benchmarks (reference)

| Algorithm | Throughput | Hardware |
|-----------|-----------|----------|
| AES-128 | 1,346 MB/s | Ryzen 9 5950X |
| AES-256 | 1,101 MB/s | Same |
| Salsa20 | 1,363 MB/s | Same |
| XOR (baseline) | 32,485 MB/s | Same |
| 3DES | 46 MB/s | Same |

Source: [kcp-go GitHub](https://github.com/xtaci/kcp-go)
