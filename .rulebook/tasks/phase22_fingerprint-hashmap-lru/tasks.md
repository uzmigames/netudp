## 1. Implementation

- [ ] 1.1 Replace `FingerprintEntry[1024]` with `FixedHashMap<uint64_t, FingerprintEntry, 2048>`
- [ ] 1.2 Key: interpret first 8 bytes of `TokenFingerprint.hash` as uint64_t
- [ ] 1.3 On insert: check if entry exists (update) or insert new; evict expired entries
- [ ] 1.4 On lookup: O(1) hash lookup instead of O(N) scan
- [ ] 1.5 Time-based cleanup: on each connection request, evict entries past expire_time
- [ ] 1.6 Remove `fingerprint_count` (hash map tracks its own size)
- [ ] 1.7 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
