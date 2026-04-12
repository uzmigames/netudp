# Proposal: Compression + Stats + DDoS — Bandwidth Optimization + Observability + Protection

## Why
Game packets are highly compressible (35-67% savings with netc). Bandwidth control (token bucket + AIMD congestion) prevents overwhelming slow clients. Comprehensive GNS-level statistics (ping, quality, throughput, queue depth, compression ratio) are essential for server operators. DDoS protection with 5 severity levels and auto-cooloff protects production servers. Automatic rekeying at 1GB/1h maintains long-session security. This phase takes netudp from "functional" to "production-ready" in terms of efficiency, observability, and security.

## What Changes
- Per-connection token bucket + QueuedBits budget + AIMD congestion control
- netc compression: stateful for reliable ordered channels, stateless for unreliable, dictionary loading
- DDoS severity escalation (None → Low → Medium → High → Critical) with auto-cooloff
- Per-connection stats (30+ fields), per-channel stats, global server stats
- Compression ratio tracking

## Impact
- Affected specs: 10-bandwidth-congestion, 11-compression, 12-statistics, 05-connect-tokens (DDoS)
- Affected code: src/bandwidth/, src/compress/, src/stats/, src/connection/ddos.h
- Breaking change: NO
- User benefit: 35-67% bandwidth savings, production-grade observability, DDoS resilience
