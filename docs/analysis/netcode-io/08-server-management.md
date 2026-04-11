# 8. Server Connection Management

## Client Slots

Fixed array of N slots (max 256). Client index = slot index. Slot contains: address, client_id, encryption keys, replay protection, confirmed flag.

## Connection Request Processing (strict order)

1. Packet != 1078 bytes → ignore
2. Version mismatch → ignore
3. Protocol ID mismatch → ignore
4. Token expired → ignore
5. Private token decrypt fails → ignore
6. Server address not in token → ignore
7. Client address already connected → ignore
8. Client ID already connected → ignore
9. Token already used by different address → ignore
10. Track token HMAC in history
11. Server full → send DENIED (smaller than request)
12. Add encryption mapping for address → respond with CHALLENGE

## Connect Token Entry Tracking

Server maintains a history of used connect token HMACs to prevent token reuse from different IPs. Entries expire with the token.

## Encryption Manager

Maps client addresses to encryption keys. Allows packet decryption before client slot assignment. Entries expire if no traffic within timeout.
