# 9. Iris — New Replication System

## Overview

Iris is UE5's new replication system, running parallel to the legacy channel/bunch system.

- Guarded by `#if UE_WITH_IRIS`
- Legacy: Actor → FObjectReplicator → Bunch → Packet
- Iris: Actor → ReplicationBridge → ReplicationSystem → different serialization

## Key Point for netudp

Iris changes the **replication logic**, not the transport. Both systems use the same:
- NetConnection
- PacketHandler pipeline
- UDP socket
- Sequence tracking / ack history

This confirms that the transport layer (what netudp provides) is independent of the replication strategy. Game engines can build any replication system on top of netudp's channels.
