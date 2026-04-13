## 1. Implementation

- [x] 1.1 Detect same-size + same-dest packets in Windows socket_send_batch
- [x] 1.2 Concatenate payloads into contiguous super-buffer on stack
- [x] 1.3 Send via WSASendTo with total_len — kernel segments by UDP_SEND_MSG_SIZE
- [x] 1.4 Fallback: if WSASendTo fails with super-buffer, fall through to per-packet loop
- [x] 1.5 NETUDP_ZONE("sock::send_uso") profiling
- [x] 1.6 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (socket.cpp comments)
- [x] 2.2 Write tests covering the new behavior (353/353 pass)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
