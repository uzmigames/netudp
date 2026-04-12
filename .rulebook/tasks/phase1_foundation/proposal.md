# Proposal: Foundation — SIMD, Sockets, Pools, Address, Basic I/O

## Why
Every layer of the netudp stack depends on four foundational subsystems: SIMD runtime dispatch (for accelerating crypto, ack processing, fragment tracking), platform socket abstraction (for cross-platform UDP I/O), zero-GC memory pools (for allocation-free hot paths), and address parsing/comparison. Without these, no encrypted connection, no reliability engine, and no benchmarks are possible. This phase delivers the "plumbing" that the entire library builds on.

## What Changes
- SIMD detection (CPUID x86, compile-time ARM) + function pointer dispatch table + scalar/SSE4.2/AVX2/NEON implementations
- Platform socket abstraction: create/bind/send/recv/close on Windows (Winsock2), Linux/macOS (BSD sockets), non-blocking mode, 4MB buffers
- Zero-GC memory: custom allocator interface, Pool<T> free-list, FixedRingBuffer<T,N>, FixedHashMap<K,V,N>
- Address type: IPv4/IPv6 parsing ("host:port"), comparison (SIMD-accelerated), hashing
- Basic send/recv: raw unencrypted UDP echo using socket + pool + address

## Impact
- Affected specs: 01-simd, 02-platform-sockets, 03-memory-pools
- Affected code: src/simd/, src/socket/, src/core/, include/netudp/
- Breaking change: NO (greenfield)
- User benefit: Cross-platform UDP I/O with SIMD acceleration and zero-allocation guarantees
