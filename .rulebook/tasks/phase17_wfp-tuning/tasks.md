## 1. Implementation

- [ ] 1.1 Detect `UDP_SEND_MSG_SIZE` availability (Windows 10 1703+) and apply on socket create
- [ ] 1.2 Apply `SetFileCompletionNotificationModes(FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)` when RIO or IOCP is active
- [ ] 1.3 Add `windows_perf_flags` to `netudp_server_config_t` (bitmask for opt-in tuning)
- [ ] 1.4 Implement `netudp_windows_check_wfp()`: detect if Base Filtering Engine is running and estimate PPS impact
- [ ] 1.5 Write `docs/guides/windows-server-tuning.md` covering: WFP disable (net stop BFE), RSS config (Set-NetAdapterRss), interrupt moderation, NIC offloads, power plan
- [ ] 1.6 Build and verify: `cmake --build build --config Release`

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
