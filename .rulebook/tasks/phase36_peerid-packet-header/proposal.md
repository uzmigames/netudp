# Proposal: phase36_peerid-packet-header

## Why

Every received data packet does TWO FNV-1a hashes of 20 bytes: `rate_limiter.allow()` + `address_to_slot.find()`. ENet uses ZERO hash computation — it embeds a 16-bit peerID in the packet header, making lookup a single array dereference: `peers[peerID]`. At 100K PPS this is ~200K unnecessary hash computations = 10-20M extra cycles/s (5-10% CPU overhead).

## What Changes

- Add 16-bit connection slot index to data packet prefix (after the existing prefix byte + nonce)
- Server dispatch: read slot index from packet, verify address matches `connections[slot].address`
- Fallback: if slot invalid or address mismatch, use `address_to_slot` hash map (connection requests)
- Client: include assigned slot index in all data packets after handshake

## Impact

- Affected code: `src/wire/frame.h` (packet layout), `src/server.cpp` (dispatch), `src/client.cpp` (send)
- Breaking change: YES (wire format change — bump protocol version)
- User benefit: 5-10% CPU reduction, O(1) array lookup per received packet
