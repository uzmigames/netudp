## 1. Implementation

- [ ] 1.1 Detect `UDP_SEGMENT` availability at socket create (setsockopt probe)
- [ ] 1.2 Store GSO capability flag in Socket struct
- [ ] 1.3 Implement `socket_send_gso()`: concatenate payloads, set UDP_SEGMENT via cmsg
- [ ] 1.4 In `socket_send_batch()` Linux path: group packets by destination, use GSO for same-dest batches
- [ ] 1.5 Handle variable-size packets: GSO requires same segment size, pad or send separately
- [ ] 1.6 Fallback: if GSO sendmsg returns error, retry without GSO
- [ ] 1.7 Add NETUDP_ZONE profiling for GSO send path
- [ ] 1.8 Build on Linux (Docker/WSL) and verify

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
