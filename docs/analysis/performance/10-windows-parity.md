# 10. Windows Performance Parity Analysis

## The Gap

```
                          Linux              Windows            Gap
sendmmsg/recvmmsg         ~500 ns/pkt        N/A                -
io_uring                  ~140 ns/pkt        N/A                -
sendto/WSASendTo          N/A                ~7,200 ns/pkt      14-51x slower
```

The Windows gap is not inherent to the OS — it comes from the socket API pattern:

```
Per-packet cost breakdown (Windows sendto):

  Kernel mode transition:        ~1,500 ns   (ring3 → ring0 → ring3)
  Socket → route lookup:         ~1,000 ns   (internal connect+send+disconnect)
  WFP callout processing:        ~2,000 ns   (Windows Filtering Platform)
  AFD driver + NDIS path:        ~2,000 ns   (protocol stack)
  Address conversion:              ~700 ns   (sockaddr per call)
  ─────────────────────────────────────────
  Total:                         ~7,200 ns
```

## Available Windows APIs

| API | PPS (published) | Overhead/pkt | Available Since |
|-----|----------------:|-------------:|-----------------|
| `sendto` loop | ~120K–138K | ~7.2 µs | WinXP |
| `WSASendTo` batch | ~138K | ~7.0 µs | WinXP |
| IOCP + overlapped | ~200K | ~5 µs | WinXP |
| TransmitPackets | ~300K | ~3 µs | WinXP (single dest only) |
| **RIO Polled** | **450K–1M** | **~1–2 µs** | **Win8 / Server 2012** |
| **RIO + WFP disabled** | **1M–2.3M** | **<1 µs** | **Win8 + admin config** |
| Windows XDP | ~2M+ (est.) | <0.5 µs | Server 2022 only |

## Registered I/O (RIO) — Primary Solution

### Architecture (mirrors io_uring)

```
io_uring (Linux):                        RIO (Windows 8+):
  App → SQ ring → Kernel → CQ ring        App → RIOSend → Kernel → RIO CQ
  Pre-registered buffers                   RIORegisterBuffer (pinned memory)
  Polled CQ (zero syscall)                 RIODequeueCompletion (zero syscall)
  io_uring_submit (batched)                RIONotify (batched flush)
```

Both eliminate:
- Kernel transition per I/O operation
- Buffer copy per packet
- Lock contention on the socket

### Published RIO Benchmarks

| Config | PPS | Hardware | Source |
|--------|----:|---------|--------|
| sendto loop polled | 122,000 | Xeon E5620, 10GbE | ServerFramework 2012 |
| IOCP traditional | 384,000 | Same | ServerFramework 2012 |
| RIO IOCP | 492,000 | Same | ServerFramework 2012 |
| RIO Polled | 482,000 | Same | ServerFramework 2012 |
| RIO single (Win10 2021) | 180,000 | 10GbE, Win10 Pro | MS Q&A |
| RIO + WFP disabled | ~1,000,000 | 10GbE | MS Q&A |
| RIO + UDP_SEND_MSG_SIZE | ~2,300,000 | 10GbE, 64K bufs | MS Q&A |

### RIO API Flow

```
Init (once):
  WSAIoctl(SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER) → RIO function table
  RIORegisterBuffer(recv_pool, size) → recv_buf_id
  RIORegisterBuffer(send_pool, size) → send_buf_id
  RIOCreateCompletionQueue(depth, NULL) → polled CQ
  RIOCreateRequestQueue(sock, recv_depth, 1, send_depth, 1, cq, cq, NULL) → RQ

Recv (zero syscall):
  Pre-post: RIOReceive(rq, &buf, 1, 0, context) × recv_depth
  Poll: n = RIODequeueCompletion(cq, results, 64)
  For each completion: read packet from registered buffer, re-post receive

Send (batched):
  For each packet: RIOSend(rq, &buf, 1, 0, NULL)
  Flush: RIONotify(cq)  // or just dequeue send completions
```

## WFP (Windows Filtering Platform) Impact

WFP adds ~2µs per packet. On dedicated servers where the operator controls the firewall:

```
Disable WFP:
  net stop BFE              # Stops Base Filtering Engine
  sc config BFE start=disabled

Impact:
  RIO without WFP:  ~1,000,000 PPS
  RIO with WFP:       ~600,000 PPS
  Delta:             +40% PPS
```

Additional tuning:
- `UDP_SEND_MSG_SIZE`: kernel-level UDP segmentation offload (Win10 1703+)
- `SetFileCompletionNotificationModes(FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)`: reduces IOCP overhead
- RSS (Receive Side Scaling): distribute packets across CPU cores at NIC level

## Projected Impact

```
MMORPG scenario (1000 players, 5 updates, 20 Hz, 100K msgs/s):

Backend           Pkts/s    CPU/s       Headroom
─────────────     ──────    ─────       ────────
sendto (now)      20,000    163 ms      6x
RIO Polled        20,000    ~39 ms      25x
RIO + WFP off     20,000    ~28 ms      36x

Windows with RIO reaches ~60% of Linux io_uring efficiency.
```

## Execution Plan

| Phase | Task | Impact | Effort |
|-------|------|--------|--------|
| 16 | RIO polled backend (`socket_rio.cpp`) | **4–8x Windows PPS** | Medium-High |
| 17 | WFP tuning + UDP_SEND_MSG_SIZE | **+20–40% on top of RIO** | Low |
| 18 | Benchmark suite: backend comparison | Measured proof | Medium |

## Sources

- [ServerFramework: RIO IOCP Performance](https://serverframework.com/asynchronousevents/2012/08/winsock-registered-io-io-completion-port-performance.html)
- [ServerFramework: RIO Performance Take 2](https://serverframework.com/asynchronousevents/2012/08/windows-8server-2012-registered-io-performance---take-2.html)
- [Microsoft Q&A: RIO Performance Degradation](https://learn.microsoft.com/en-us/answers/questions/268716/has-winsock-registered-io-performance-degraded-sin)
- [Microsoft Learn: Registered I/O Extensions](https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-server-2012-r2-and-2012/hh997032(v=ws.11))
- [Grijjy: High-Performance UDP Servers](https://blog.grijjy.com/2018/08/29/creating-high-performance-udp-servers-on-windows-and-linux/)
- [Microsoft: MsQuic + XDP Balance](https://techcommunity.microsoft.com/blog/networkingblog/balance-performance-in-msquic-and-xdp/3627665)
- [GitHub: microsoft/xdp-for-windows](https://github.com/microsoft/xdp-for-windows)
