# 5. Encryption — XOR Cipher

**File:** `src/utils/uws-adapter.ts` (lines 119-127)

## Implementation

```typescript
decryptMessage(encryptedData: Uint8Array, key: string): Uint8Array {
    const keyBytes = new TextEncoder().encode(key);
    const decryptedBytes = new Uint8Array(encryptedData.length);
    
    for (let i = 0; i < encryptedData.length; i++)
        decryptedBytes[i] = encryptedData[i] ^ keyBytes[i % keyBytes.length];
    
    return decryptedBytes;
}
```

## Analysis

**This is NOT real encryption.** XOR with a repeating key is trivially breakable (known-plaintext attack, frequency analysis). It's closer to obfuscation.

The key (`diffPublicKey`) appears to be exchanged during the Diffie-Hellman handshake but used as a simple XOR key rather than as input to a proper cipher.

## Packet Format

```
Byte 0: Encryption flag
  0 = plaintext (only auth packets allowed)
  1 = XOR "encrypted" with session key

Bytes 1..N: Payload (XOR'd if flag = 1)
```

## Security Comparison

| Server | Encryption | Strength |
|---|---|---|
| Server1 | AES-128-GCM (disabled) | Strong but unused |
| Server5 | ChaCha20-Poly1305 | Strong, always on |
| This (MMORPG) | XOR cipher | Trivially breakable |

## Relevance to netudp

Reinforces that netudp MUST provide real AEAD encryption (ChaCha20-Poly1305) by default. Application developers who roll their own "encryption" tend to implement something like this XOR approach. The library should make the secure path the easy path.
