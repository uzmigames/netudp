# Proposal: phase22_fingerprint-hashmap-lru

## Why

The fingerprint cache (`server.cpp:776`) is a linear array of 1024 entries scanned on every connection request. When full, new tokens are silently dropped — no eviction of expired entries. Under DDoS or high connection churn, this means: O(1024) scan per request, stale entries blocking new connections, and potential crash when cache fills. Replacing with a hash map + time-based eviction fixes all three.

## What Changes

- Replace `FingerprintEntry fingerprint_cache[1024]` with `FixedHashMap<uint64_t, FingerprintEntry, 2048>`
- Key: first 8 bytes of fingerprint hash (already a uint64)
- Time-based eviction: on insert, evict entries past expire_time
- O(1) lookup instead of O(N) scan
- O(1) insert with automatic eviction of expired entries

## Impact

- Affected code: `src/server.cpp` (fingerprint check, insert, cleanup)
- Breaking change: NO
- User benefit: 5-10% CPU reduction on connection-heavy servers, no silent token drops
