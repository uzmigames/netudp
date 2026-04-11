# 7. Relevance to netudp

## Transport-Layer Patterns Worth Noting

### 1. QueueBuffer batching is universal

Even over TCP/WebSocket (where there's no MTU concern), the team implemented packet batching with timer-driven flush. This confirms that **batching is essential at every transport layer**. netudp should provide a first-class batching API.

### 2. Sentinel-delimited sub-packets

The `0xFEFEFEFE` delimiter approach is simple but wastes 4 bytes per sub-message and requires scanning. netudp's approach (length-prefixed sub-messages or MTU-limited batching like Server1) is superior.

### 3. Weak encryption = no encryption

The XOR "encryption" is a lesson: if the library doesn't provide real crypto, developers will implement something broken. netudp MUST make AEAD encryption the default, not an opt-in.

### 4. JSON serialization over binary

This server uses JSON for most packets (`putString(JSON.stringify(data))`). This is 5-10x more bandwidth than binary serialization but acceptable over TCP. Over UDP with MTU limits, binary serialization is mandatory. netudp provides the buffer — the application serializes.

### 5. WebSocket as fallback consideration

This server exists because browser clients need WebSocket. netudp is a native UDP library, but a future companion library could provide WebSocket fallback for browser clients using the same API.

### 6. uWebSockets.js performance

The choice of uWebSockets.js (C++ engine) over pure-JS `ws` shows that even in Node.js, native performance matters for game servers. Validates the decision to write netudp in C.

## What Does NOT Apply to netudp

- NestJS gateway pattern (framework-specific)
- Redis pub/sub for multi-server (infrastructure)
- Bull job queues (async task processing)
- JSON serialization (application choice)
- WebSocket framing (TCP transport)
- XOR "encryption" (replaced by real AEAD)
- All 135 game packet types (application-level)
- Entity/Player/Item systems (game logic)

## Summary

This server's main contribution to netudp design is confirming that:
1. **Batching is non-negotiable** (even over TCP they needed it)
2. **Default-on encryption is critical** (devs will use XOR otherwise)
3. **Native C performance is the right call** (even Node.js reaches for C++ engines)
