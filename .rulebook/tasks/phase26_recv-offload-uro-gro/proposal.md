# Proposal: phase26_recv-offload-uro-gro

## Why

Each recvfrom/WSARecvFrom returns one datagram, requiring one syscall per incoming packet. MsQuic enables `UDP_RECV_MAX_COALESCED_SIZE` (Windows URO) and Cloudflare enables `UDP_GRO` (Linux GRO) so the NIC/kernel coalesces multiple datagrams into a single recv buffer — reducing recv syscalls by 2-5x. Our recv_batch already loops up to 64 times, but each iteration is still one datagram; with offloading, one iteration can return multiple coalesced datagrams.

Source: `docs/analysis/udp/03-recv-path-optimization.md`

## What Changes

- Windows: set `UDP_RECV_MAX_COALESCED_SIZE = UINT16_MAX - 8` on socket create
- Windows recv: detect coalesced recv (len > MTU), split by segment size, process each
- Linux: set `UDP_GRO = 1` on socket create
- Linux recv: detect GRO coalesced recv, split by segment size
- Recv buffer size increased to handle coalesced payloads

## Impact

- Affected code: `src/socket/socket.cpp` (socket_create, recv_batch)
- Breaking change: NO (graceful fallback if option not supported)
- User benefit: 2-5x fewer recv syscalls under load
