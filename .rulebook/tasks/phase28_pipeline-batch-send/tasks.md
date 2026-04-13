## 1. Implementation

- [x] 1.1 Pre-allocate `SocketPacket batch[kSocketBatchMax]` + batch_storage on send thread stack
- [x] 1.2 Drain up to kSocketBatchMax packets from send_queue into batch array
- [x] 1.3 Call `socket_send_batch(socket, batch, count)` once per drain cycle
- [x] 1.4 Add NETUDP_ZONE("pipe::send_drain") profiling
- [x] 1.5 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (server.cpp comments)
- [x] 2.2 Write tests covering the new behavior (353/353 pass)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
