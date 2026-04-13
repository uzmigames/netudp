# Proposal: phase25_socket-options-tier1

## Why

Industry analysis (docs/analysis/udp/) revealed critical socket options that MsQuic, netcode.io, and Cloudflare use that netudp is missing. `SIO_UDP_CONNRESET` is a correctness bug — without it, ICMP port-unreachable from disconnected clients causes recv failures that drop packets for other clients. `IP_DONTFRAGMENT` avoids per-packet PMTU discovery overhead. `IP_TOS` DSCP 46 enables QoS on managed networks. `SO_RCVBUF` increase prevents burst drops.

Source: `docs/analysis/udp/01-socket-options-gap.md`

## What Changes

- Add `SIO_UDP_CONNRESET = FALSE` on Windows socket create (3 lines)
- Add `IP_DONTFRAGMENT = TRUE` on Windows socket create (2 lines)
- Add `IP_TOS = 0x2E` (DSCP 46 Expedited Forwarding) on all platforms
- Increase `SO_RCVBUF` default to 16MB (from 4MB caller-specified)

## Impact

- Affected code: `src/socket/socket.cpp` (socket_create)
- Breaking change: NO
- User benefit: Prevents recv failures, reduces overhead, enables QoS, prevents burst drops
