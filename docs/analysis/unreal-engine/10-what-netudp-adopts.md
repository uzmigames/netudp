# 10. What netudp Should Adopt from UE5

## Adopt

| # | Pattern | UE5 Source | How netudp Adapts |
|---|---------|-----------|-------------------|
| 1 | **14-bit packet sequence + 256-bit ack history** | FNetPacketNotify | netudp uses 16-bit seq + 32-bit ack_bits (lighter, sufficient for game tick rates) |
| 2 | **Per-channel reliable sequencing** | Channel OutRec/InRec | Each netudp channel has independent sequence tracking |
| 3 | **Stateless handshake** (no server state before auth) | StatelessConnectHandlerComponent | netudp uses netcode.io connect tokens (same principle, richer) |
| 4 | **Packet simulation** (lag, loss, dup, reorder) | FPacketSimulationSettings | Built into netudp network simulator |
| 5 | **Token bucket bandwidth control** per connection | QueuedBits / CurrentNetSpeed | netudp's per-connection token bucket |
| 6 | **Keepalive packets advance ack window** | FlushNet empty packets | netudp sends keepalive with piggybacked acks |
| 7 | **Handshake versioning** | EHandshakeVersion enum | netudp version in connect token AAD |
| 8 | **DDoS detection** with escalating severity | FDDoSDetection | netudp's token bucket per IP (simpler but effective) |
| 9 | **Secret rotation** for handshake | 2 rotating HMAC secrets | netudp can rotate connect token private keys |
| 10 | **Sequence window overflow protection** | MaxSequenceHistoryLength check | netudp blocks sends when ack window full |
| 11 | **Bit-level serialization** | FBitWriter/FBitReader | netudp buffer has bit-level read/write (from Server5 FlatBuffer) |
| 12 | **Connection state machine with graceful close** | USOCK_Closing waits for acks | netudp sends redundant disconnect packets |

## Learn From (But Do Better)

| # | UE5 Weakness | netudp Improvement |
|---|---|---|
| 1 | **Whole-bunch retransmit** on partial loss | Fragment-level bitmask — retransmit only lost fragments |
| 2 | **No active congestion control** (just token bucket) | Loss-based AIMD congestion avoidance |
| 3 | **No compression by default** (Oodle is proprietary plugin) | netc integrated, open-source, beats Oodle on small packets |
| 4 | **AES-256-GCM only** (requires AES-NI or OpenSSL) | ChaCha20-Poly1305 default (fast everywhere, vendored) |
| 5 | **UObject-based connections** (GC overhead) | Zero-GC connection pool |
| 6 | **Channel limit of 8** (hard-coded via 3-bit index) | Configurable channels (default 4, up to 255) |
| 7 | **No ack delay field** for RTT | Every packet carries ack delay for continuous RTT (from GNS) |
| 8 | **PacketHandler loaded from INI** (over-engineered) | Fixed compile-time pipeline (simpler, faster) |
| 9 | **No built-in compression benchmarks** | Benchmark-driven development with CI regression |

## Do NOT Copy

| # | UE5 Pattern | Why Not |
|---|---|---|
| 1 | UObject/GC for networking types | Zero-GC is our core principle |
| 2 | INI-driven PacketHandler loading | Over-engineered for a transport library |
| 3 | Iris replication system | Game-level, not transport-level |
| 4 | Actor Channel replication | Game-specific, not agnostic |
| 5 | Blueprint networking | Scripting-level integration (not transport) |
| 6 | NMT_* control messages | Game-specific protocol, not transport |
| 7 | ReliabilityHandlerComponent | Deprecated in UE 5.3+ |

## Summary

UE5's networking is battle-tested at Fortnite scale. The transport-level patterns (stateless handshake, sequence tracking, per-channel reliability, bandwidth control, packet simulation) are solid and proven. But the implementation is heavily coupled to UObject/GC, over-engineered for configurability, and uses proprietary compression (Oodle). netudp takes the proven patterns and implements them cleanly — zero-GC, open-source compression (netc), modern crypto (ChaCha20), and SIMD-accelerated throughout.
