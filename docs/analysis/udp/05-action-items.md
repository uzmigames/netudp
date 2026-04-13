# 05. Action Items — Prioritized Fixes

## Tier 1: Immediate (1-line fixes in socket_create)

These are correctness and free-performance fixes. No reason not to do them.

| # | Fix | Platform | Lines | Impact |
|---|-----|----------|-------|--------|
| 1 | `SIO_UDP_CONNRESET = FALSE` | Windows | 3 | Prevents recv failures on client disconnect |
| 2 | `IP_DONTFRAGMENT = TRUE` | Windows | 2 | Avoids PMTU discovery overhead |
| 3 | `IP_TOS = 0x2E` (DSCP 46) | All | 2 | QoS Expedited Forwarding |
| 4 | Increase `SO_RCVBUF` to 16MB | All | 1 | Prevents burst drops |

## Tier 2: Socket Options (medium lines, high impact)

| # | Fix | Platform | Impact |
|---|-----|----------|--------|
| 5 | `UDP_RECV_MAX_COALESCED_SIZE` | Windows 10+ | 2-5x fewer recv syscalls (URO) |
| 6 | `UDP_GRO = 1` | Linux 5.0+ | 2-5x fewer recv syscalls (GRO) |
| 7 | Connected socket for client | All | -1-3µs per client send |

## Tier 3: Send Path Redesign (biggest PPS impact)

| # | Fix | Platform | Impact |
|---|-----|----------|--------|
| 8 | Batch sends in pipeline send thread | All | ~2x send throughput |
| 9 | GSO super-buffer send (`UDP_SEGMENT` cmsg) | Linux 4.18+ | 5-10x send throughput |
| 10 | True USO super-buffer send | Windows 10+ | 5-10x send throughput |
| 11 | Per-CPU socket affinity (`SIO_CPU_AFFINITY`) | Windows | Better RSS distribution |

## Tier 4: Recv Path Enhancement

| # | Fix | Platform | Impact |
|---|-----|----------|--------|
| 12 | Handle URO coalesced recv (split by segment size) | Windows | Completes URO support |
| 13 | Handle GRO coalesced recv | Linux | Completes GRO support |

## Expected Combined Impact

```
Current:     ~75-80K PPS (Windows, Zig CC, single-thread)

After Tier 1: ~80-85K PPS (+5%, less recv failures, less PMTU overhead)
After Tier 2: ~90-110K PPS (+20-30%, fewer recv syscalls)
After Tier 3: ~150-300K PPS (2-4x, batched+offloaded sends)

With pipeline threads + all tiers:
  Windows: ~200-400K PPS
  Linux:   ~500K-1M PPS (sendmmsg + GSO)
```

## Comparison After Fixes

```
                        Current    After All Fixes    Industry Best
netudp (Windows)        80K        200-400K           ~1M (MsQuic RIO+USO)
netudp (Linux)          —          500K-1M            ~7M (io_uring+GSO)
ENet (no crypto)        184K       184K               184K (no room to grow)
GNS (rate-capped)       7K         7K                 7K (design limitation)
netcode.io              ~100K*     ~100K              ~100K (minimal design)
.NET 8 raw socket       81K        81K                81K (managed code)

*netcode.io PPS is estimated — no published benchmarks.
```

## Sources

- [MsQuic datapath_winuser.c](https://github.com/microsoft/msquic/blob/main/src/platform/datapath_winuser.c)
- [Cloudflare: Accelerating UDP for QUIC](https://blog.cloudflare.com/accelerating-udp-packet-transmission-for-quic/)
- [Cloudflare: Everything about UDP sockets](https://blog.cloudflare.com/everything-you-ever-wanted-to-know-about-udp-sockets-but-were-afraid-to-ask-part-1/)
- [netcode.io source](https://github.com/mas-bandwidth/netcode)
- [ENet protocol.c](https://github.com/lsalzman/enet/blob/master/protocol.c)
- [GNS v1.3.0 release](https://github.com/ValveSoftware/GameNetworkingSockets/releases/tag/v1.3.0)
