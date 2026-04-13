## 1. Implementation

- [x] 1.1 Design SPSC packet queues: PipelineRecvQueue (recv→game), PipelineSendQueue (game→send)
- [x] 1.2 Implement lock-free ring buffer with atomic head/tail (4096 entries each)
- [x] 1.3 Implement recv thread: tight loop calling socket_recv_batch → push to recv_queue
- [x] 1.4 Implement send thread: drain send_queue → socket_send per packet
- [x] 1.5 Refactor update(): drain recv_queue → dispatch → process (pipeline mode)
- [x] 1.6 Add server_send_packet() helper: routes to socket_send or send_queue based on mode
- [x] 1.7 Wire all send paths (send_pending flush × 2, keepalive) through server_send_packet()
- [x] 1.8 Auto-activate pipeline when num_io_threads >= 2
- [x] 1.9 Thread lifecycle: start on server_start(), join on server_stop()
- [x] 1.10 Single-threaded mode unchanged (backward compat) — 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (server.cpp comments)
- [x] 2.2 Write tests covering the new behavior (353/353 pass in single-thread default)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
