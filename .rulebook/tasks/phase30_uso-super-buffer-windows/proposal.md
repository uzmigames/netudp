# Proposal: phase30_uso-super-buffer-windows

## Why

netudp already sets `UDP_SEND_MSG_SIZE` on Windows but sends individual packets — not actually using USO (UDP Segmentation Offload). MsQuic achieves ~1M+ PPS by passing a single large buffer containing multiple concatenated datagrams to WSASendMsg with the segment size specified. The NIC hardware segments them into individual UDP packets. This amortizes the kernel transition cost across many logical packets.

Source: `docs/analysis/udp/02-send-path-optimization.md`

## What Changes

- Implement `socket_send_uso()`: concatenate same-dest payloads into one WSASendMsg call
- Use existing `UDP_SEND_MSG_SIZE` socket option as segment size hint
- WSASendMsg with single WSABUF containing concatenated payloads
- Only applies when sending multiple packets to same destination with same payload size
- Integrate with server send_pending: after coalescing, if multiple connections have same-size packets, batch via USO

## Impact

- Affected code: `src/socket/socket.cpp` (Windows send path)
- Breaking change: NO (transparent, fallback to WSASendTo)
- User benefit: 5-10x Windows send throughput for batched sends
