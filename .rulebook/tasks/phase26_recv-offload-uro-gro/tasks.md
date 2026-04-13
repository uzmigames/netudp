## 1. Implementation

- [ ] 1.1 Windows: set `UDP_RECV_MAX_COALESCED_SIZE = UINT16_MAX - 8` in socket_create
- [ ] 1.2 Linux: set `UDP_GRO = 1` via setsockopt(SOL_UDP) in socket_create
- [ ] 1.3 Increase recv buffer allocation to handle coalesced payloads (up to 64KB)
- [ ] 1.4 Windows recv_batch: detect recv len > NETUDP_MAX_PACKET_ON_WIRE, split by segment size
- [ ] 1.5 Linux recv_batch: detect GRO coalesced recv via cmsg, split by segment size
- [ ] 1.6 Add NETUDP_ZONE profiling for coalesced recv splitting
- [ ] 1.7 Graceful fallback: if setsockopt fails (old OS), continue with normal recv
- [ ] 1.8 Build and verify on both Windows and Linux

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
