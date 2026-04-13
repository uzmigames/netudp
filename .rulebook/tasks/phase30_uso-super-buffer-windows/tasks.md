## 1. Implementation

- [ ] 1.1 Implement `socket_send_uso()`: accepts array of same-size payloads for same dest
- [ ] 1.2 Concatenate payloads into single contiguous buffer
- [ ] 1.3 Call WSASendMsg with WSABUF pointing to concatenated buffer
- [ ] 1.4 Kernel + NIC segment by `UDP_SEND_MSG_SIZE` into individual datagrams
- [ ] 1.5 In `socket_send_batch()` Windows path: detect same-dest batches, use USO
- [ ] 1.6 Fallback: if WSASendMsg fails or payloads differ in size, use WSASendTo loop
- [ ] 1.7 Add NETUDP_ZONE profiling for USO send path
- [ ] 1.8 Benchmark: USO vs individual WSASendTo
- [ ] 1.9 Build and verify on Windows

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
