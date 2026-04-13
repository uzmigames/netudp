# Proposal: phase29_gso-super-buffer-linux

## Why

Linux UDP GSO (Generic Segmentation Offload, `UDP_SEGMENT`) lets the application send one large buffer that the kernel segments into MTU-sized datagrams — one syscall for up to 64 packets. Cloudflare measured 2.5x throughput improvement (640 Mbps → 1.6 Gbps) using sendmmsg + GSO. Combined with our existing sendmmsg, each entry in the batch can itself be a GSO super-buffer, dispatching hundreds of datagrams per syscall.

Source: `docs/analysis/udp/02-send-path-optimization.md`

## What Changes

- Detect `UDP_SEGMENT` support at socket create (Linux 4.18+)
- New `socket_send_gso()`: concatenate same-destination payloads into super-buffer
- Set `UDP_SEGMENT` via cmsg ancillary data on sendmsg
- Integrate with `socket_send_batch()`: when multiple packets go to same dest and same size, use GSO
- Fallback: if GSO not available, use existing sendmmsg path

## Impact

- Affected code: `src/socket/socket.cpp` (Linux send path), `src/socket/socket.h`
- Breaking change: NO (opt-in, transparent fallback)
- User benefit: 5-10x Linux send throughput for same-dest batches
