# Performance Analysis

**Date:** 2026-04-12
**Status:** Active planning document

Competitive analysis, bottleneck identification, and optimization roadmap for netudp v1.0.0.

## Index

| # | Document | Summary |
|---|----------|---------|
| 01 | [Current State](01-current-state.md) | Profiling zone results, derived metrics, key bottleneck (sock::send 7.2µs) |
| 02 | [Competitive Landscape](02-competitive-landscape.md) | Published benchmarks: ENet, GNS, KCP, LiteNetLib, .NET 8, Unreal, Unity |
| 03 | [Competitive Position](03-competitive-position.md) | Honest assessment vs competition, feature matrix, strengths |
| 04 | [Encryption Analysis](04-encryption-analysis.md) | AES-256-GCM vs XChaCha20-Poly1305, per-packet cost, HW acceleration |
| 05 | [Gap Analysis](05-gap-analysis.md) | What's missing for 2M PPS, <1ms latency, 100K connections |
| 06 | [Frame Coalescing](06-frame-coalescing.md) | Critical missing optimization: multi-frame packing per UDP packet |
| 07 | [Optimization Roadmap](07-optimization-roadmap.md) | Phases A–G with implementation plans, expected gains, risks |
| 08 | [Game Server Scenarios](08-game-server-scenarios.md) | PPS budgets for FPS, Battle Royale, MMO, high-density sim |
| 09 | [Sources](09-sources.md) | All external references with URLs |
| 10 | [Windows Parity](10-windows-parity.md) | RIO backend, WFP tuning, Windows vs Linux gap analysis |
| 11 | [Server Bottlenecks](11-server-bottlenecks.md) | O(N) dispatch, single-thread pipeline, active list, fingerprint hash |
