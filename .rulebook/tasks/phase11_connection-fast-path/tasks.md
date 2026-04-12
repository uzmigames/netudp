## 1. Implementation

- [ ] 1.1 Benchmark current `conn::init` and `conn::reset` with profiler (baseline)
- [ ] 1.2 Modify `Pool::release()` to zero-fill the released slot before marking free
- [ ] 1.3 Modify `Pool::acquire()` to omit zero-fill (already clean from release)
- [ ] 1.4 Verify Connection struct initialization still works (all fields clean on acquire)
- [ ] 1.5 Benchmark after change (target: acquire < 1us)
- [ ] 1.6 Build and verify: `cmake --build build --config Release`

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
