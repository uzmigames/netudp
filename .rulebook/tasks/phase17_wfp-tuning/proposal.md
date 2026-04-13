# Proposal: phase17_wfp-tuning

## Why

Windows Filtering Platform (WFP) adds ~2µs per packet to every send/recv — accounting for ~28% of the total 7.2µs sendto cost. Published RIO benchmarks show 40% PPS improvement when WFP is disabled. For dedicated game server deployments where the operator controls the firewall, netudp should provide guidance and tooling to minimize WFP overhead. Additionally, `UDP_SEND_MSG_SIZE` socket option (Windows 10 1703+) enables kernel-level UDP segmentation offload, and `SetFileCompletionNotificationModes(FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)` reduces IOCP overhead for synchronous completions.

Source: Microsoft Q&A RIO benchmarks, ServerFramework blog

## What Changes

- Add `netudp_server_config_t::windows_perf_flags` bitmask for opt-in Windows tuning
- Apply `UDP_SEND_MSG_SIZE` on socket create when available (coalesced kernel sends)
- Apply `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS` when using IOCP path
- Add `docs/guides/windows-server-tuning.md` documenting: WFP disable (BFE service), RSS configuration, interrupt moderation, NDIS settings
- Add `netudp_windows_check_wfp()` diagnostic function to detect if WFP is impacting performance

## Impact

- Affected specs: socket layer (Windows tuning)
- Affected code: `src/socket/socket.cpp` (socket options), new `docs/guides/windows-server-tuning.md`
- Breaking change: NO
- User benefit: +20-40% PPS on Windows dedicated servers with proper tuning
