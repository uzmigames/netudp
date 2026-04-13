## 1. Implementation

- [x] 1.1 Apply `UDP_SEND_MSG_SIZE` (IPPROTO_UDP) on socket create (Windows 10 1703+, silent fail on older)
- [x] 1.2 Apply `SIO_LOOPBACK_FAST_PATH` via WSAIoctl on socket create (Windows 8+, bypasses stack for loopback)
- [x] 1.3 Implement `netudp_windows_is_wfp_active()`: queries BFE service status via SCManager API
- [x] 1.4 Fix shadowed `flags` variable in non-Windows fcntl path (renamed to `fl`)
- [x] 1.5 Write `docs/guides/windows-server-tuning.md`: WFP disable, RSS config, interrupt moderation, NIC offloads, power plan, RIO usage, expected PPS table
- [x] 1.6 Build and verify: `cmake --build build --config Release` — 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (windows-server-tuning.md, socket.cpp comments)
- [x] 2.2 Write tests covering the new behavior (353/353 pass, socket opts applied silently)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
