# 04. Implementation Comparison

## How Each Library Structures Their Hot Path

### ENet

```
enet_host_service()
  ├─ enet_protocol_receive_incoming_commands()
  │   └─ recvfrom() in loop until EWOULDBLOCK
  │       └─ dispatch by peer address (O(peers) linear scan)
  ├─ enet_protocol_send_outgoing_commands()
  │   └─ For each peer with pending commands:
  │       ├─ Aggregate commands into single datagram (up to MTU)
  │       └─ sendmsg() with iovec scatter-gather (1 syscall per peer)
  └─ Return events to application

Key: 1 syscall per peer per tick, NOT 1 per command.
No crypto. No batching across peers. Simple.
```

### GameNetworkingSockets (Valve)

```
SteamNetworkingSockets::RunCallbacks()
  ├─ Poll groups → each group on own thread (v1.3+)
  ├─ Per connection:
  │   ├─ Recv: poll socket → decrypt → deliver
  │   └─ Send: queue → encrypt → rate limit → sendto()
  └─ Lock: per-connection lock (v1.3), was global lock (pre-v1.3)

Key lesson: Global lock was THE bottleneck. Fine-grained locking = 3-4x improvement.
Crypto: AES-256-GCM (hardware accelerated).
Socket: plain sendto()/recvfrom(), nothing fancy.
```

### netcode.io (Glenn Fiedler)

```
netcode_server_update()
  ├─ recvfrom() in tight loop until EWOULDBLOCK
  │   └─ process_packet() inline (no queueing)
  ├─ For each client:
  │   ├─ Timeout check
  │   └─ Send keepalive/response
  └─ Return

Key: Absolute minimal code path. No abstraction layers.
Socket options: SO_SNDBUF/RCVBUF 4MB, SIO_UDP_CONNRESET, IP_TOS.
Crypto: libsodium (XSalsa20-Poly1305 for tokens, nothing per-packet after handshake).
```

### MsQuic (Microsoft)

```
CxPlatDataPathProcessCqe()  // IOCP completion
  ├─ Recv: pre-posted WSARecvMsg completions
  │   ├─ URO: one completion = multiple coalesced datagrams
  │   └─ Split by UDP_RECV_MAX_COALESCED_SIZE
  ├─ Send: WSASendMsg with overlapped I/O
  │   ├─ USO: one send = multiple datagrams (kernel segments)
  │   └─ Per-CPU socket affinity via SIO_CPU_AFFINITY
  └─ Per-processor design: 1 socket per CPU core

Key: USO + URO + per-CPU affinity. No RIO (IOCP instead).
Achieves ~1M+ PPS through offloads, not fancy APIs.
```

### Cloudflare QUIC (Linux)

```
quiche::send()
  ├─ Prepare datagrams
  ├─ GSO: concatenate same-size payloads into super-buffer
  ├─ sendmmsg() with UDP_SEGMENT cmsg per entry
  │   └─ Each entry: up to 64 segments × MTU bytes
  └─ Total: one syscall → hundreds of datagrams

quiche::recv()
  ├─ recvmmsg() with GRO enabled
  │   └─ Each entry may contain coalesced datagrams
  └─ Split by segment size

Key: sendmmsg + GSO + GRO. 2.5x throughput improvement measured.
```

## Feature Comparison Matrix

| Feature | netudp | ENet | GNS | netcode.io | MsQuic | Cloudflare |
|---------|--------|------|-----|-----------|--------|-----------|
| Frame coalescing | Yes (11.4x) | Yes | Yes | No | N/A (QUIC) | N/A |
| sendmmsg (Linux) | Yes | No | No | No | Yes | Yes |
| GSO (Linux) | **No** | No | No | No | Yes | **Yes** |
| GRO (Linux) | **No** | No | No | No | Yes | **Yes** |
| USO (Windows) | Partial* | No | No | No | **Yes** | N/A |
| URO (Windows) | **No** | No | No | No | **Yes** | N/A |
| RIO (Windows) | Yes | No | No | No | No | N/A |
| IOCP | No | No | No | No | Yes | N/A |
| SIO_UDP_CONNRESET | **No** | No | No | **Yes** | **Yes** | N/A |
| IP_DONTFRAGMENT | **No** | No | No | No | **Yes** | N/A |
| IP_TOS / DSCP | **No** | No | No | **Yes** | Yes | Yes |
| Connected client socket | **No** | No | No | No | No | No |
| Per-CPU affinity | **No** | No | No | No | **Yes** | Yes |
| O(1) addr dispatch | Yes | No | Yes | No | N/A | N/A |
| Active conn list | Yes | Yes | Yes | Yes | N/A | N/A |
| Pipeline threads | Yes | No | Yes (v1.3) | No | Yes | Yes |

*netudp sets `UDP_SEND_MSG_SIZE` but sends individual packets, not USO super-buffers.

## What We Should Prioritize

```
Immediate (1-line fixes):
  1. SIO_UDP_CONNRESET        ← correctness fix, not optional
  2. IP_DONTFRAGMENT          ← free performance
  3. IP_TOS DSCP 46           ← QoS on managed networks

Quick wins (socket options):
  4. UDP_RECV_MAX_COALESCED_SIZE (Windows URO)
  5. UDP_GRO (Linux)
  6. SO_RCVBUF increase

Medium effort (send optimization):
  7. Connected socket for client
  8. Batch sends in pipeline thread
  9. GSO super-buffer send (Linux)
  10. True USO super-buffer send (Windows)
```
