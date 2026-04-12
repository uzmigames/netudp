# Proposal: phase9_frame-coalescing

## Why

`server_send_pending()` sends 1 message per UDP packet. Each packet incurs 33 bytes wire overhead, 1 encrypt (948ns), and 1 sendto syscall (7,231ns). For an MMORPG sending 100K msgs/s, syscalls alone consume 720ms/s — impossible on a single core. Frame coalescing packs multiple frames into one UDP packet, cutting syscalls, crypto, and bandwidth by ~5×. The wire format already supports multi-frame payloads; only the send loop needs changing.

Source: `docs/analysis/performance/06-frame-coalescing.md`

## What Changes

- Rewrite `server_send_pending()` in `src/server.cpp` to accumulate frames in a payload buffer up to MTU, then encrypt and send once
- AckFields written once per coalesced packet (not per frame)
- Flush triggers: MTU full, no more pending messages, or explicit `netudp_server_flush()` call
- Client send path (`client_send_pending`) gets the same treatment
- No wire format changes — receive parser already iterates frames via `peek_frame_type()`

## Impact

- Affected specs: spec 09 (wire format — no change needed), server send pipeline
- Affected code: `src/server.cpp` (server_send_pending), `src/client.cpp` (client_send_pending if exists)
- Breaking change: NO
- User benefit: 5x message throughput, 46% bandwidth reduction on small messages, MMORPG viability on single core
