## 1. Implementation

- [x] 1.1 Detect same-size + same-dest packets in Linux socket_send_batch
- [x] 1.2 Concatenate payloads into contiguous super-buffer on stack
- [x] 1.3 Set UDP_SEGMENT via cmsg ancillary data (segment size = packet size)
- [x] 1.4 Send via sendmsg with GSO cmsg — one syscall for N datagrams
- [x] 1.5 Fallback: if GSO sendmsg fails, fall through to standard sendmmsg path
- [x] 1.6 NETUDP_ZONE("sock::send_gso") profiling
- [x] 1.7 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (socket.cpp comments)
- [x] 2.2 Write tests covering the new behavior (353/353 pass, GSO path tested on Linux)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
