# 03. Recv Path Optimization

## Current Recv Path (netudp)

```
Single-thread mode:
  socket_recv_batch() → up to 64 recvfrom/WSARecvFrom calls → dispatch each

Pipeline mode:
  Recv thread: socket_recv_batch() → push to recv_queue (SPSC ring)
  Game thread: pop from recv_queue → dispatch → process
```

## What We're Missing

### Windows: UDP Receive Offload (URO)

```c
// MsQuic sets this — NIC coalesces incoming datagrams
DWORD coalesced_size = UINT16_MAX - 8;
setsockopt(sock, IPPROTO_UDP, UDP_RECV_MAX_COALESCED_SIZE,
           (char*)&coalesced_size, sizeof(coalesced_size));

// Effect: instead of receiving 10 individual packets of 1200 bytes,
// recv returns ONE buffer of 12000 bytes with 10 coalesced datagrams.
// Application must split by segment size.
```

### Linux: GRO (Generic Receive Offload)

```c
int enable = 1;
setsockopt(sock, SOL_UDP, UDP_GRO, &enable, sizeof(enable));

// Effect: kernel coalesces consecutive datagrams from same source
// into one large recv buffer. Same concept as Windows URO.
```

### SIO_UDP_CONNRESET (Windows — Critical Fix)

```c
// Without this, if a client sends to our port and we respond,
// but the client's port is now closed (hard disconnect),
// the ICMP "port unreachable" response causes our NEXT recvfrom
// to fail with WSAECONNRESET instead of returning data.
// This silently drops packets for OTHER clients.

BOOL new_behavior = FALSE;
DWORD bytes;
WSAIoctl(sock, SIO_UDP_CONNRESET,
         &new_behavior, sizeof(new_behavior),
         NULL, 0, &bytes, NULL, NULL);
```

netcode.io, MsQuic, and most serious UDP servers set this.

### Larger Receive Buffer

```c
// netudp: 4MB (good)
// MsQuic: MAXINT32 (maximum possible)
// netcode.io: 4MB

// Under burst load (1000 clients all send at once), 4MB may not be enough.
// Increasing to 16MB or higher prevents kernel-side packet drops.
int buf = 16 * 1024 * 1024;  // 16MB
setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&buf, sizeof(buf));
```

## Impact Assessment

| Optimization | Platform | Expected Recv Improvement |
|-------------|----------|--------------------------|
| SIO_UDP_CONNRESET | Windows | Prevents recv failures (correctness, not perf) |
| UDP_RECV_MAX_COALESCED_SIZE | Windows | 2-5x fewer recv syscalls |
| UDP_GRO | Linux | 2-5x fewer recv syscalls |
| Larger SO_RCVBUF | All | Prevents burst drops (reliability, not perf) |

## Sources

- [Microsoft: UDP Receive Coalescing](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/udp-rsc-offload)
- [Cloudflare: Everything about UDP sockets](https://blog.cloudflare.com/everything-you-ever-wanted-to-know-about-udp-sockets-but-were-afraid-to-ask-part-1/)
- [netcode.io: socket options](https://github.com/mas-bandwidth/netcode)
