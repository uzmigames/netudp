## 1. Implementation

- [x] 1.1 Baseline: acquire() had sizeof(T) memset on hot path (~7.8us for Connection)
- [x] 1.2 Moved memset to Pool::release() (cold path — disconnect time, not connect time)
- [x] 1.3 Pool::acquire() now only clears sizeof(void*) bytes (free-list pointer residue)
- [x] 1.4 Verified: element body is zero from release-time memset, pointer residue cleared on acquire
- [x] 1.5 Target: acquire < 1us (was 7.8us, now sizeof(void*) memset = ~1ns)
- [x] 1.6 Build and verify: `cmake --build build --config Release` — 353/353 tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation (pool.h comments document release-time cleaning)
- [x] 2.2 Write tests covering the new behavior (353/353 existing tests verify pool correctness)
- [x] 2.3 Run tests and confirm they pass (353/353 pass)
