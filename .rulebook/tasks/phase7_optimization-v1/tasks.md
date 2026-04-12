## 1. Batch I/O (P7-A) — spec 02
- [ ] 1.1 Implement recvmmsg on Linux: src/socket/socket_unix.cpp socket_recv_batch() using recvmmsg() syscall, pre-allocate mmsghdr + iovec arrays, return count of received datagrams
- [ ] 1.2 Implement sendmmsg on Linux: socket_send_batch() using sendmmsg() syscall, batch up to 64 datagrams per syscall
- [ ] 1.3 Ensure loop fallback on Windows/macOS: socket_send_batch/socket_recv_batch call sendto/recvfrom in loop when platform lacks mmsg syscalls
- [ ] 1.4 Wire batch recv into server update(): replace per-packet recv loop with batch recv, process all received in single iteration
- [ ] 1.5 Tests: test_batch_io.cpp — send 100 packets via batch, receive via batch, verify all arrive

## 2. Batch Public API (P7-B) — spec 13
- [ ] 2.1 Implement netudp_server_send_batch(): accept array of {client_index, channel, data, bytes, flags}, queue all in one call
- [ ] 2.2 Implement netudp_server_receive_batch(): receive messages from all clients up to max_messages, return array of netudp_message_t*
- [ ] 2.3 Tests: test_batch_api.cpp — batch send to multiple clients, batch receive from multiple clients

## 3. Examples (P7-C)
- [ ] 3.1 Create examples/echo_server.c: minimal echo server — init, create server, register on_connect/on_disconnect callbacks, in update loop: receive all → echo back, print stats every 5s
- [ ] 3.2 Create examples/echo_client.c: connect with token, send messages, verify echoed response matches, report RTT
- [ ] 3.3 Create examples/chat_server.c: multi-client chat — on data received, broadcast_except to all other clients, reliable ordered channel for messages, unreliable for typing indicators
- [ ] 3.4 Create examples/stress_test.c: spawn 1000 client connections (in threads or async), each sends 10 messages/sec, server broadcasts state updates, run for 60s, report: total messages, packet loss, memory usage, PPS
- [ ] 3.5 Add examples/CMakeLists.txt: build all examples, link netudp

## 4. v1.0 Release (P7-D)
- [ ] 4.1 Cache-line alignment: align hot structs (Connection, Channel, PacketTracker) to 64-byte boundaries with alignas(64), verify with static_assert
- [ ] 4.2 Final API review: verify all public functions in netudp.h match spec 13, no missing functions, no extra functions, all error codes documented
- [ ] 4.3 Full test pass: ALL tests green on Windows + Linux + macOS
- [ ] 4.4 Benchmark pass: PPS ≥ 2M, latency p99 ≤ 5µs, memory ≤ 100MB (1024 connections), SIMD ≥ 20% over scalar
- [ ] 4.5 Update README.md: verify Quick Example compiles, verify architecture diagram matches implementation
- [ ] 4.6 Write CHANGELOG.md v1.0.0 entry with all features
- [ ] 4.7 Tag git v1.0.0

## 5. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 5.1 Update documentation: CHANGELOG.md, README.md (verify examples section)
- [ ] 5.2 All tests pass on all 3 platforms
- [ ] 5.3 Run clang-tidy — zero warnings across entire codebase
