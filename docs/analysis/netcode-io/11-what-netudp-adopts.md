# 11. What netudp Should Adopt from netcode.io

## Adopt Directly

| # | Feature | Why |
|---|---------|-----|
| 1 | **Connect token system** | Industry standard, proven, separates auth from game server |
| 2 | **Challenge/response handshake** | Prevents IP spoofing with minimal server state |
| 3 | **Separate client-to-server / server-to-client keys** | Prevents reflection attacks |
| 4 | **ChaCha20-Poly1305 AEAD** | Fast, constant-time, no AES-NI dependency |
| 5 | **Sequence number as nonce** | Deterministic, no random generation needed |
| 6 | **256-entry replay protection** | Robust window for real packet loss |
| 7 | **Variable-length sequence encoding** | Saves bytes in prefix |
| 8 | **Protocol version in AAD** | Prevents cross-version attacks |
| 9 | **Opaque handle API** (`create`/`destroy`/`update`) | Clean C API pattern |
| 10 | **Custom allocator support** | Game engines need this |
| 11 | **Explicit time parameter** | No hidden clock, deterministic |
| 12 | **Network simulator for testing** | Essential for CI/testing |
| 13 | **Redundant disconnect packets** | Unreliable channel needs redundancy for disconnect |
| 14 | **Vendor crypto library** | No external dependency |
| 15 | **4 MB socket buffers** | Handles burst traffic |
| 16 | **Response < Request rule** | Anti-amplification |

## Adopt the API Style

```c
// netcode.io pattern that netudp should follow:
netudp_server_t * netudp_server_create(config);
void netudp_server_update(server, double time);
void netudp_server_send(server, client_index, channel, data, len);
uint8_t * netudp_server_receive(server, client_index, int * bytes, ...);
void netudp_server_free_packet(server, packet);
void netudp_server_destroy(server);
```
