# Complete Analysis — Unreal Engine 5.6 Networking Transport Layer

Analysis of UE5's transport-level networking implementation (not replication/gameplay).

**Source:** `W:\UE_5.6_Source\Engine\`  
**Version:** UE 5.6  
**Analysis date:** 2026-04-11  
**Focus:** Transport patterns relevant for netudp (not UObject replication, not Blueprints)

---

## Index

1. [Architecture: NetDriver → NetConnection → Channel → Bunch → Packet](01-architecture.md)
2. [Packet Format & Sequence Tracking](02-packet-format.md)
3. [Reliability: Dual-Layer (Packet + Bunch)](03-reliability.md)
4. [PacketHandler Pipeline (Middleware Chain)](04-packet-handler-pipeline.md)
5. [Stateless Handshake (DTLS-Inspired)](05-stateless-handshake.md)
6. [Encryption & DDoS Protection](06-encryption-ddos.md)
7. [Congestion & Bandwidth Control](07-congestion-control.md)
8. [Channel System & Multiplexing](08-channel-system.md)
9. [Iris — New Replication System](09-iris.md)
10. [What netudp Should Adopt from UE5](10-what-netudp-adopts.md)
