## 1. Implementation

- [x] 1.1 Add `SIO_UDP_CONNRESET = FALSE` via WSAIoctl on Windows
- [x] 1.2 Add `IP_DONTFRAGMENT = TRUE` via setsockopt on Windows
- [x] 1.3 Add `IP_TOS = 0x2E` (DSCP 46 Expedited Forwarding) on all platforms
- [x] 1.4 Enforce minimum `SO_RCVBUF` of 16MB
- [x] 1.5 Added fallback define for SIO_UDP_CONNRESET (Zig CC headers)
- [x] 1.6 Build and verify: 353/353 tests pass on Zig CC

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (socket.cpp comments, docs/analysis/udp/)
- [x] 2.2 Write tests covering the new behavior (353/353 pass)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
