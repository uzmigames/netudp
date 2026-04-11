# 6. Replay Protection

## Implementation

```c
#define NETCODE_REPLAY_PROTECTION_BUFFER_SIZE 256

struct netcode_replay_protection_t {
    uint64_t most_recent_sequence;
    uint64_t received_packet[256];  // Stores actual sequence at each index
};
```

## Algorithm

```c
int netcode_replay_protection_already_received(replay_protection, sequence) {
    // 1. If sequence is too old (> 256 behind most recent), reject
    if (sequence + 256 <= most_recent_sequence)
        return 1;  // too old

    // 2. Check if already received at this index
    int index = sequence % 256;
    if (received_packet[index] == UINT64_MAX)
        return 0;  // slot empty, not received

    // 3. If stored sequence matches, it's a duplicate
    if (received_packet[index] >= sequence)
        return 1;  // already received

    return 0;  // not received
}

void netcode_replay_protection_advance_sequence(replay_protection, sequence) {
    // Update most recent
    if (sequence > most_recent_sequence)
        most_recent_sequence = sequence;

    // Mark as received
    int index = sequence % 256;
    received_packet[index] = sequence;
}
```

## Design Choice: uint64 Array vs Bitmask

netcode.io uses a **256-entry uint64 array** instead of a bitmask:
- Each slot stores the full 64-bit sequence number
- Comparison: `received_packet[seq % 256] >= seq`
- Memory: 256 × 8 = 2048 bytes per client

Server5 used a **64-bit bitmask**:
- Compact: 8 bytes per client
- But only 64-packet window

netcode.io's approach is more robust for real-world packet loss patterns.

## Applied To

Replay protection is checked for:
- Connection keep-alive packets
- Connection payload packets
- Connection disconnect packets

NOT checked for:
- Connection request packets (not encrypted, validated differently)
- Connection challenge packets (client-only)
- Connection response packets (server validates by decryption)
