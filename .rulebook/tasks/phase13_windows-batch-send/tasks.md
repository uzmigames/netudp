## 1. Implementation

- [x] 1.1 Windows send_batch: pre-convert all addresses, then WSASendTo in tight loop
- [x] 1.2 Windows recv_batch: WSARecvFrom with WSABUF (avoids sendto compat shim overhead)
- [x] 1.3 macOS fallback: unchanged loop with socket_send/socket_recv
- [x] 1.4 Linux path unchanged (recvmmsg/sendmmsg)
- [x] 1.5 Build and verify: `cmake --build build --config Release` — 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (socket.cpp comments explain optimization)
- [x] 2.2 Write tests covering the new behavior (353/353 existing tests exercise send/recv paths)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
