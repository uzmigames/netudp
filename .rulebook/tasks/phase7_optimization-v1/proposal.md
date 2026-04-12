# Proposal: Optimization + v1.0 Release — Batch I/O, Examples, Production Release

## Why
The final stretch to v1.0: Linux-specific optimizations (recvmmsg/sendmmsg for batch I/O), public batch send/receive API for applications, production-quality examples (echo server, chat, stress test), and the full release checklist. The examples serve as both documentation and integration tests. The stress test validates 1000+ concurrent connections. This phase gates the "production-ready" label.

## What Changes
- recvmmsg/sendmmsg batch I/O on Linux (loop fallback on Windows/macOS)
- Batch send/receive public API
- 3 example programs: echo server, chat server, stress test (1000 connections)
- Cache-line alignment optimization for hot structs
- v1.0 release: API documentation, migration guide from netcode.io, CHANGELOG, git tag

## Impact
- Affected specs: 13-public-api (batch API), 14-benchmarks (stress test)
- Affected code: src/socket/ (batch), src/api.cpp (batch API), examples/
- Breaking change: NO
- User benefit: Maximum I/O throughput on Linux, ready-to-use examples, stable v1.0 API
