# Proposal: phase39_bypass-ratelimit-known

## Why

Every incoming packet — including from already-authenticated connected clients — goes through `rate_limiter.allow()`: FNV-1a hash of 20-byte AddressKey + hash map probe with memcmp. ENet does zero per-packet rate limiting. The rate limiter exists to protect against DDoS from unknown sources, not to throttle established connections. After `address_to_slot` lookup confirms a known peer, the rate limiter check is redundant.

## What Changes

- Reorder dispatch: do `address_to_slot.find()` FIRST (or peerID lookup after phase 36)
- If peer is known (slot found, address matches): process directly, no rate_limiter call
- If peer is unknown: rate_limiter.allow() + connection request path (existing behavior)

## Impact

- Affected code: `src/server.cpp` (server_dispatch_packet, recv loop)
- Breaking change: NO
- User benefit: 2-3% CPU reduction (eliminate hash+probe for every known-peer packet)
