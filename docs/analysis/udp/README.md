# UDP Industry Best Practices Analysis

**Date:** 2026-04-13
**Purpose:** Identify what high-performance UDP implementations do that netudp is missing.

## Index

| # | Document | Summary |
|---|----------|---------|
| 01 | [Socket Options Gap](01-socket-options-gap.md) | Missing options from MsQuic, netcode.io, Cloudflare |
| 02 | [Send Path Optimization](02-send-path-optimization.md) | GSO, USO, connected sockets, batch send thread |
| 03 | [Recv Path Optimization](03-recv-path-optimization.md) | URO, GRO, coalesced receive |
| 04 | [Implementation Comparison](04-implementation-comparison.md) | ENet, GNS, netcode.io, MsQuic, Cloudflare patterns |
| 05 | [Action Items](05-action-items.md) | Prioritized fixes with expected impact |
