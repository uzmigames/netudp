## 1. Implementation

- [ ] 1.1 Add `FixedRingBuffer<SocketPacket, 64>` send queue to Socket struct (Windows only)
- [ ] 1.2 Implement `socket_queue_send()` that buffers packets instead of calling `sendto`
- [ ] 1.3 Implement `socket_flush_send()` that calls `WSASend` with `WSABUF` array
- [ ] 1.4 Add flush triggers: queue full, explicit `netudp_server_flush()`, or timer
- [ ] 1.5 Add `flush_interval_us` config to `netudp_server_config_t`
- [ ] 1.6 Benchmark: `WSASend` array vs `sendto` loop vs current
- [ ] 1.7 Ensure Linux/macOS path is unchanged (compile guards)
- [ ] 1.8 Build and verify: `cmake --build build --config Release`

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
