# 02. Send Path Optimization

## Current Send Path (netudp)

```
Per connection, per tick:
  server_send_pending()
    → coalesce frames into payload buffer (up to MTU)
    → packet_encrypt() (XChaCha20-Poly1305)
    → server_send_packet()
      → pipeline mode: send_queue.push() → send thread → socket_send()
      → single mode: socket_send() directly
```

### Problem: send thread sends one packet at a time

```cpp
// pipeline_send_thread (server.cpp:255)
for (int i = 0; i < kSocketBatchMax; ++i) {
    if (!server->send_queue->pop(&pkt)) break;
    socket_send(&server->socket, &pkt.addr, pkt.data, pkt.len);  // 1 syscall per packet!
}
```

Should use `socket_send_batch()` to batch multiple packets per syscall.

## What MsQuic Does (Gold Standard)

### UDP Segmentation Offload (USO)

```
Without USO: 10 packets → 10 sendto calls → 10 kernel transitions
With USO:    10 packets → 1 WSASendMsg call → NIC segments into 10 datagrams

Requirement: all 10 packets must be same size (or padded to same size)
Socket option: UDP_SEND_MSG_SIZE = segment_size (already set in netudp)
Send call: WSASendMsg with buffer = concatenated_payloads, len = total_len
```

netudp sets `UDP_SEND_MSG_SIZE` but doesn't actually USE the USO path — still sends individual packets.

### Per-CPU Socket Affinity

```
MsQuic server:
  Socket 0 → CPU 0 (SIO_CPU_AFFINITY)
  Socket 1 → CPU 1
  Socket 2 → CPU 2
  Socket 3 → CPU 3

Kernel RSS distributes incoming packets by 5-tuple hash to matching CPU.
Each socket only receives packets for its CPU — zero cross-CPU contention.
```

## What Cloudflare Does (Linux GSO)

### UDP_SEGMENT (Generic Segmentation Offload)

```c
// Set segment size via cmsg ancillary data
struct cmsghdr *cm;
cm->cmsg_level = SOL_UDP;
cm->cmsg_type = UDP_SEGMENT;
*(uint16_t *)CMSG_DATA(cm) = segment_size;  // e.g. 1200

// Send one large buffer — kernel splits into MTU-sized datagrams
char super_buffer[1200 * 64];  // 64 packets concatenated
sendmsg(fd, &msg, 0);  // ONE syscall → 64 datagrams
```

### sendmmsg + GSO Combined

```c
// Each entry in sendmmsg can itself be a GSO super-buffer
struct mmsghdr msgs[4];  // 4 entries
// Each entry: 16 segments × 1200 bytes = 19,200 bytes per entry
// Total: 4 × 16 = 64 datagrams per syscall
sendmmsg(fd, msgs, 4, 0);
```

Cloudflare measured: 640 Mbps → 1.6 Gbps (2.5x) with sendmmsg + GSO.

## What ENet Does

ENet packs multiple protocol commands into a single datagram before the syscall. This is the same concept as netudp's frame coalescing — but ENet also uses `sendmsg()` with `iovec` scatter-gather to avoid copying fragmented command buffers into a contiguous block.

## Connected Socket (Client Only)

```c
// Instead of:
sendto(sock, data, len, 0, &server_addr, addr_len);  // route lookup every call

// Do:
connect(sock, &server_addr, addr_len);  // once
send(sock, data, len, 0);              // cached route, ~1-3µs faster per call
```

netcode.io doesn't do this, but for a single-server client it's free performance.

## Fixes for netudp

| Fix | Platform | Impact | Effort |
|-----|----------|--------|--------|
| Batch sends in pipeline send thread | All | ~2x send throughput | Low |
| Connected socket for client | All | -1-3µs per send | Low |
| Implement USO send path (Windows) | Windows | ~5-10x send throughput | Medium |
| Implement GSO send path (Linux) | Linux | ~5-10x send throughput | Medium |
| Per-CPU socket affinity | Windows | Better RSS distribution | Low |

## Sources

- [Cloudflare: Accelerating UDP for QUIC](https://blog.cloudflare.com/accelerating-udp-packet-transmission-for-quic/)
- [MsQuic datapath_winuser.c](https://github.com/microsoft/msquic/blob/main/src/platform/datapath_winuser.c)
- [ENet protocol.c](https://github.com/lsalzman/enet/blob/master/protocol.c)
