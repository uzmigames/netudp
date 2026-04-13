## 1. Implementation

- [x] 1.1 Windows: set `UDP_RECV_MAX_COALESCED_SIZE = 65527` (UINT16_MAX - 8) in socket_create
- [x] 1.2 Linux: set `UDP_GRO = 1` via setsockopt(SOL_UDP) in socket_create
- [x] 1.3 Fallback defines for UDP_RECV_MAX_COALESCED_SIZE and UDP_GRO (Zig CC headers)
- [x] 1.4 Graceful: setsockopt silently ignored on older OS versions
- [x] 1.5 Build and verify: 353/353 tests pass on Zig CC

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (socket.cpp comments, analysis docs)
- [x] 2.2 Write tests covering the new behavior (353/353 pass)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
