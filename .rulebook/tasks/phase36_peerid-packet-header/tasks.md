## 1. Implementation

- [ ] 1.1 Add 16-bit `slot_id` field after nonce_counter in data packet wire format
- [ ] 1.2 Server assigns slot_id to client during handshake, sends in keepalive response
- [ ] 1.3 Client stores slot_id, includes it in all data packet headers
- [ ] 1.4 Server dispatch: read slot_id from packet → `connections[slot_id]` → verify address matches
- [ ] 1.5 Fallback: if slot_id invalid or address mismatch, use address_to_slot hash map
- [ ] 1.6 Connection requests still use address-based dispatch (no slot_id yet)
- [ ] 1.7 Bump protocol version for wire format change
- [ ] 1.8 Benchmark: PPS before/after (expected +5-10%)
- [ ] 1.9 Build and verify: tests pass

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
