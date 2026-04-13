## 1. Implementation

- [ ] 1.1 Add `SIO_UDP_CONNRESET = FALSE` via WSAIoctl on Windows (prevents ICMP port-unreachable from killing recvfrom)
- [ ] 1.2 Add `IP_DONTFRAGMENT = TRUE` via setsockopt on Windows (avoids PMTU discovery overhead)
- [ ] 1.3 Add `IP_TOS = 0x2E` (DSCP 46 Expedited Forwarding) on all platforms
- [ ] 1.4 Increase minimum `SO_RCVBUF` to 16MB when caller specifies less
- [ ] 1.5 Add fallback defines for constants missing in Zig CC headers
- [ ] 1.6 Build and verify: 353/353 tests pass on both Zig CC and MSVC

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
