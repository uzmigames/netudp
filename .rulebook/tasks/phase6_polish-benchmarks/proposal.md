# Proposal: Polish + Benchmarks — Network Simulator, Packet Interfaces, Rekeying, Performance Suite

## Why
Before v1.0 release, the library needs: a network simulator for testing under adverse conditions (loss, latency, jitter, reorder), clean packet interfaces for game servers (handler registration, lifecycle callbacks, zero-copy buffer API), automatic rekeying for long sessions, and a comprehensive benchmark suite proving performance targets (2M PPS, p99 ≤ 5µs). CI benchmark regression detection ensures no future commit degrades performance. This is the "hardening" phase.

## What Changes
- Network simulator: configurable latency, jitter, loss, duplicate, reorder, incoming lag
- Packet handler registration API (callback per packet type) + connection lifecycle callbacks
- Buffer acquire/send (zero-copy) + buffer read/write helpers (u8-u64, f32/f64, varint, string)
- Broadcast / multicast helpers
- Automatic rekeying: REKEY flag in prefix byte, HKDF-SHA256 derivation, epoch management
- Benchmark suite: PPS, latency, SIMD compare, scalability, memory, CI regression

## Impact
- Affected specs: 13-public-api, 14-benchmarks, 15-network-simulator, 04-crypto (rekeying)
- Affected code: src/sim/, src/api.cpp (handlers + buffer API), bench/
- Breaking change: NO
- User benefit: Battle-tested under simulated adverse conditions, proven performance, clean game server APIs
