# 5. SecureSession — X25519 + ChaCha20-Poly1305

**File:** `Core/Network/Security/SecureSession.cs` (551 lines)

## Overview

Complete upgrade from Server1's ECDH P-521 + AES-128-GCM (which was disabled in production). Server5 uses modern, faster primitives that are **always enabled**.

## Session State

```csharp
public unsafe struct SecureSession {
    public fixed byte TxKey[32];         // Send encryption key
    public fixed byte RxKey[32];         // Receive decryption key
    public fixed byte SessionSalt[16];   // For rekeying
    public ulong SeqTx;                  // Transmit sequence (nonce)
    public ulong SeqRx;                  // Receive sequence
    public uint ConnectionId;            // Used in nonce generation

    // Replay protection
    private ulong _replayWindow;         // 64-bit sliding window
    private ulong _highestSeqReceived;

    // Rekey tracking
    public ulong BytesTransmitted;
    public DateTime SessionStartTime;
}
```

## Key Exchange — X25519 (vs P-521 in Server1)

```csharp
// Server side:
var serverPriv = new X25519PrivateKeyParameters(rng);  // 32-byte private key
var serverPub = serverPriv.GeneratePublicKey().GetEncoded();  // 32-byte public key

// Shared secret via X25519 Diffie-Hellman:
var agreement = new X25519Agreement();
agreement.Init(serverPriv);
byte[] sharedSecret = new byte[32];
agreement.CalculateAgreement(clientPub, sharedSecret, 0);

// Key derivation via HKDF-SHA256:
var hkdf = new HkdfBytesGenerator(new Sha256Digest());
hkdf.Init(new HkdfParameters(sharedSecret, salt, "ToS-UE5 v1"));
hkdf.GenerateBytes(okm);  // 64 bytes → 32 TxKey + 32 RxKey
```

**Key advantages over Server1:**
- X25519 keys are **32 bytes** (vs P-521's ~130 bytes per coordinate)
- X25519 is **~10x faster** than P-521
- HKDF with labeled info string ("ToS-UE5 v1") for proper domain separation
- Separate Tx/Rx keys (Server1 used same key both directions)

## Nonce Generation

```csharp
public void GenerateNonce(ulong sequence, Span<byte> nonce) {
    // 12-byte nonce = ConnectionId (4B) || Sequence (8B)
    BinaryPrimitives.WriteUInt32LittleEndian(nonce, ConnectionId);
    BinaryPrimitives.WriteUInt64LittleEndian(nonce.Slice(4), sequence);
}
```

**Nonce is deterministic** — derived from ConnectionId + sequence counter. Never reuses because sequence is monotonically increasing. No random nonce needed (unlike Server1).

## Encryption with Optional LZ4 Compression

```csharp
public bool EncryptPayloadWithCompression(ReadOnlySpan<byte> plaintext, 
    ReadOnlySpan<byte> aad, Span<byte> result, out int resultLength, out bool wasCompressed) {
    
    // 1. Encrypt with ChaCha20-Poly1305
    EncryptPayload(plaintext, aad, encrypted, out encryptedLength);
    
    // 2. If encrypted payload > 512 bytes, try LZ4 compression
    if (encryptedLength > 512) {
        int compressedLength = LZ4.Compress(encrypted, encryptedLength, result, result.Length);
        if (compressedLength > 0 && compressedLength < encryptedLength) {
            wasCompressed = true;
            resultLength = compressedLength;
            return true;  // Compressed is smaller → use it
        }
    }
    
    // 3. Fallback: use uncompressed encrypted data
    encrypted.CopyTo(result);
    resultLength = encryptedLength;
    return true;
}
```

## Replay Protection (NEW — Server1 had none)

```csharp
private const int ReplayWindowSize = 64;

private bool IsSequenceValid(ulong sequence) {
    if (sequence > _highestSeqReceived) return true;  // Newer than anything seen
    if (sequence + ReplayWindowSize <= _highestSeqReceived) return false;  // Too old
    
    ulong diff = _highestSeqReceived - sequence;
    ulong mask = 1UL << (int)diff;
    return (_replayWindow & mask) == 0;  // Not seen in window
}

private void UpdateReplayWindow(ulong sequence) {
    if (sequence > _highestSeqReceived) {
        ulong shift = sequence - _highestSeqReceived;
        _replayWindow = (shift >= 64) ? 1 : (_replayWindow << (int)shift) | 1;
        _highestSeqReceived = sequence;
    } else {
        ulong diff = _highestSeqReceived - sequence;
        _replayWindow |= 1UL << (int)diff;
    }
}
```

## Automatic Rekeying (NEW — Server1 had none)

```csharp
private const ulong RekeyBytesThreshold = 1UL << 30;  // 1 GB
private static readonly TimeSpan RekeyTimeThreshold = TimeSpan.FromMinutes(60);

public bool ShouldRekey() {
    return BytesTransmitted >= RekeyBytesThreshold ||
           (DateTime.UtcNow - SessionStartTime) >= RekeyTimeThreshold;
}

public bool PerformRekey() {
    // HKDF with "rekey" context + max(SeqTx, SeqRx)
    // Derives new TxKey + RxKey from SessionSalt
    // Resets all counters and replay window
}
```
