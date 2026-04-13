# 01. Socket Options Gap

Options that high-performance implementations use that netudp is missing.

## What We Have

| Option | Status | Where |
|--------|--------|-------|
| `SO_REUSEADDR` | Set | socket.cpp |
| `SO_SNDBUF` / `SO_RCVBUF` (4MB) | Set | socket.cpp |
| `IPV6_V6ONLY = 0` | Set | socket.cpp |
| `SO_REUSEPORT` (Linux) | Set | socket.cpp (when multi-socket) |
| `SIO_LOOPBACK_FAST_PATH` (Windows) | Set | socket.cpp |
| `UDP_SEND_MSG_SIZE` (Windows USO) | Set | socket.cpp |
| Non-blocking mode | Set | socket.cpp |

## What We're Missing

### Critical (MsQuic uses these)

| Option | Platform | Effect | Who Uses It |
|--------|----------|--------|-------------|
| `SIO_UDP_CONNRESET` | Windows | Prevents ICMP port-unreachable from killing recvfrom. Without this, if a client hard-disconnects, next recvfrom fails with WSAECONNRESET, disrupting the recv batch loop. | netcode.io, MsQuic |
| `UDP_RECV_MAX_COALESCED_SIZE` | Windows 10+ | NIC coalesces incoming datagrams into one large recv buffer (URO). MsQuic sets to UINT16_MAX - 8. | MsQuic |
| `IP_DONTFRAGMENT` | All | Avoids PMTU discovery overhead per packet. We already use MTU 1200. | MsQuic |
| `SO_RCVBUF = MAXINT32` | All | MsQuic uses maximum possible. Our 4MB may be insufficient under burst. | MsQuic |

### High Impact (Cloudflare/Linux)

| Option | Platform | Effect | Who Uses It |
|--------|----------|--------|-------------|
| `UDP_SEGMENT` (GSO) | Linux 4.18+ | Kernel segments one large buffer into MTU-sized datagrams. One syscall sends 64 datagrams. Combined with sendmmsg: one syscall sends hundreds. | Cloudflare QUIC |
| `UDP_GRO` | Linux 5.0+ | Kernel coalesces incoming datagrams. Reduces per-packet processing in recv path. | Cloudflare QUIC |
| `IP_TOS = 0x2E` (DSCP 46) | All | QoS marking (Expedited Forwarding). Routers prioritize game traffic. | netcode.io |

### Medium Impact

| Option | Platform | Effect | Who Uses It |
|--------|----------|--------|-------------|
| `SIO_CPU_AFFINITY` | Windows | Binds socket to specific CPU core for RSS distribution. | MsQuic |
| Connected socket (client) | All | `connect()` + `send()` instead of `sendto()`. Saves route lookup ~1-3us/pkt on client. | Best practice |

## Per-Packet Cost Breakdown (Windows sendto)

```
Component               Cost        Avoidable?
────────────────────    ────        ──────────
User-kernel transition  ~1-2 µs    RIO, IOCP overlap
Route resolution        ~1-3 µs    Connected socket (client only)
Buffer copy             ~0.5-1 µs  RIO pre-registered buffers
Protocol stack (UDP+IP) ~1-2 µs    USO (amortize across segments)
NIC driver queue        ~0.5-1 µs  No
WFP callouts            ~0-2 µs    Disable BFE service

Total: ~5-11 µs (varies by config)
Our measured: ~7-9 µs (sendto), ~20 µs (Zig CC bench)
```

## Implementation Priority

```
1. SIO_UDP_CONNRESET          ← 1 line fix, prevents recv failures
2. IP_DONTFRAGMENT            ← 1 line fix, saves PMTU overhead
3. SO_RCVBUF increase         ← 1 line fix, prevents burst drops
4. UDP_RECV_MAX_COALESCED_SIZE ← URO, major recv improvement
5. Connected socket (client)   ← saves route lookup per packet
6. UDP_SEGMENT (Linux GSO)     ← biggest Linux send optimization
7. UDP_GRO (Linux)             ← recv coalescing
8. IP_TOS DSCP marking         ← QoS on managed networks
```

## Sources

- [MsQuic datapath_winuser.c](https://github.com/microsoft/msquic/blob/main/src/platform/datapath_winuser.c)
- [netcode.io source](https://github.com/mas-bandwidth/netcode)
- [Cloudflare: Accelerating UDP for QUIC](https://blog.cloudflare.com/accelerating-udp-packet-transmission-for-quic/)
