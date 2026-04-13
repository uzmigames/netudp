## 1. Implementation

- [x] 1.1 Rewrite `server_send_pending()` to accumulate multiple frames in payload buffer before encrypt+send
- [x] 1.2 Add flush logic: flush on MTU full, queue empty, or explicit `netudp_server_flush()` call
- [x] 1.3 Write AckFields once per coalesced packet, not per frame
- [x] 1.4 Handle reliable frame tracking (packet_tracker) correctly for multi-frame packets
- [x] 1.5 Apply same coalescing to client send path (`client_send_pending`)
- [x] 1.6 Add `frames_coalesced` counter to ConnectionStats
- [x] 1.7 Add NETUDP_ZONE profiling for coalescing loop (`srv::coalesce`, `cli::coalesce`)
- [x] 1.8 Build and verify: `cmake --build build --config Release` — 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation (analysis at docs/analysis/performance/06-frame-coalescing.md)
- [x] 2.2 Write tests covering the new behavior (test_frame_coalescing.cpp: 3 tests)
- [x] 2.3 Run tests and confirm they pass (353/353 pass, 0 failures)
