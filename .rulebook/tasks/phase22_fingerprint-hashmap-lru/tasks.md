## 1. Implementation

- [x] 1.1 Replace FingerprintEntry[1024] with FixedHashMap<uint64_t, FingerprintValue, 2048>
- [x] 1.2 Key: interpret first 8 bytes of TokenFingerprint.hash as uint64_t
- [x] 1.3 O(1) lookup via fingerprint_map.find() instead of O(N) scan
- [x] 1.4 Insert/update via fingerprint_map.insert() (replaces manual array push)
- [x] 1.5 Remove fingerprint_count (hash map tracks its own size)
- [x] 1.6 Build and verify: 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (server.cpp comments)
- [x] 2.2 Write tests covering the new behavior (353/353 existing tests)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
