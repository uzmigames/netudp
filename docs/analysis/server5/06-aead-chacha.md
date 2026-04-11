# 6. AeadChaCha — Low-Level Crypto

**File:** `Core/Network/Security/AeadChaCha.cs` (159 lines)

Standalone AEAD Seal/Open using ChaCha20-Poly1305 with ArrayPool for zero-allocation. Nonce = ConnectionId(4B) || SeqTx(8B). AAD = full 14-byte PacketHeader. All temp arrays zeroed before return.
