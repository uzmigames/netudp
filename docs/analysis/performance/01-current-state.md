# 01. Current State (netudp v1.0.0)

**Measured on:** Windows 10 / i7-12700K, loopback, single thread, Release build (MSVC).

## Profiling Zone Results

| Zone | Avg (ns) | Notes |
|------|----------|-------|
| `sock::send` | **7,231** | `sendto` syscall — Windows IOCP/AFD driver overhead |
| `conn::init` | **379,000** | One-time cost; pool pre-allocates at startup |
| `conn::reset` | **7,800** | Zero-fills Connection struct + sub-objects |
| `crypto::packet_encrypt` | **948** | XChaCha20-Poly1305 full pipeline |
| `crypto::packet_decrypt` | **1,020** | Includes replay check |
| `aead::encrypt` | **800** | `crypto_aead_lock` (monocypher) |
| `aead::decrypt` | **780** | `crypto_aead_unlock` (monocypher) |
| `crypto::replay` | **24** | Bitmask sliding window (64-bit) |
| `crypto::nonce` | **20** | 24-byte LE encoding |
| `wire::unreliable_frame` | **~15** | Frame header write (4 bytes + memcpy) |
| `wire::reliable_frame` | **~18** | Frame header write (6 bytes + memcpy) |
| `chan::queue_send` | **~30** | Ring buffer push + sequence assign |
| `chan::dequeue_send` | **~25** | Ring buffer pop |

## Derived Metrics

| Metric | Value | Platform |
|--------|-------|----------|
| PPS (single thread, no crypto) | ~138,000 | Windows loopback |
| PPS (single thread, with crypto) | ~105,000 | Windows loopback (estimated) |
| PPS (batch, Linux, no crypto) | ~2,000,000 | recvmmsg/sendmmsg, theoretical |
| Crypto throughput (1200-byte pkt) | ~1.27 GB/s | monocypher XChaCha20-Poly1305 |
| Per-packet encrypt overhead | 948 ns | ~0.95 µs |
| Per-packet decrypt overhead | 1,020 ns | ~1.02 µs |

## Key Bottleneck

The dominant cost on Windows is the `sock::send` syscall at **7.2 µs per packet**:

```
Theoretical max PPS (single thread, Windows) = 1s / 7.2µs = ~138,888 pps
```

This is a **syscall budget problem**, not a library problem. The kernel's UDP send path on Windows (IOCP/AFD driver) cannot be made faster without:

1. **Frame coalescing** — multiple messages per UDP packet (see [06-frame-coalescing.md](06-frame-coalescing.md))
2. **Batch syscalls** — Linux `sendmmsg` (already implemented), Windows has no equivalent
3. **Kernel bypass** — DPDK, WinDPDK (extreme engineering)
4. **Horizontal scaling** — multi-threaded, one socket per thread

## Per-Packet Cost Breakdown

```
Single message lifecycle (current implementation):

  chan::queue_send        30 ns   ░
  chan::dequeue_send      25 ns   ░
  wire::unreliable_frame 15 ns   ░
  crypto::nonce          20 ns   ░
  crypto::packet_encrypt 948 ns  ████████
  sock::send           7,231 ns  ████████████████████████████████████████████████████████████

  Total:              ~8,269 ns  per message → ~121K messages/s max

  Of which:
    Syscall: 87.4%  ←── THE bottleneck
    Crypto:  11.5%
    Framing:  1.1%
```

With frame coalescing (5 messages per packet):

```
  5× chan operations     275 ns  ██
  5× wire frames          75 ns  ░
  1× crypto::encrypt     948 ns  ████████
  1× sock::send        7,231 ns  ████████████████████████████████████████████████████████████

  Total:              ~8,529 ns  for 5 messages → ~586K messages/s
  Improvement: 4.8× throughput (messages/s), same syscall count
```
