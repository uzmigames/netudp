## 1. Implementation

- [ ] 1.1 Allocate `SocketPacket batch[kSocketBatchMax]` in send thread stack
- [ ] 1.2 Drain up to kSocketBatchMax packets from send_queue into batch array
- [ ] 1.3 Call `socket_send_batch(socket, batch, count)` once per drain cycle
- [ ] 1.4 Add NETUDP_ZONE profiling for batched send path
- [ ] 1.5 Benchmark: pipeline batch vs pipeline individual (before/after)
- [ ] 1.6 Build and verify: tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
