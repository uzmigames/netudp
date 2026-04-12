## 1. Implementation

- [ ] 1.1 Rewrite `server_send_pending()` to accumulate multiple frames in payload buffer before encrypt+send
- [ ] 1.2 Add flush logic: flush on MTU full, queue empty, or explicit `netudp_server_flush()` call
- [ ] 1.3 Write AckFields once per coalesced packet, not per frame
- [ ] 1.4 Handle reliable frame tracking (packet_tracker) correctly for multi-frame packets
- [ ] 1.5 Apply same coalescing to client send path if applicable
- [ ] 1.6 Add `frames_coalesced` counter to ConnectionStats
- [ ] 1.7 Add NETUDP_ZONE profiling for coalescing loop (`srv::coalesce`)
- [ ] 1.8 Build and verify: `cmake --build build --config Release`

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
