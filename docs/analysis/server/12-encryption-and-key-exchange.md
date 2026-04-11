# Encryption and Key Exchange

**Files:** `Connection.DiffieHellman.cs` (87 lines), `ByteBuffer.cs` (lines 107-240)

### 12.1 Algorithm

- **Key Exchange:** ECDH over curve **NIST P-521** (BouncyCastle)
- **Derivacao:** SHA-256 of shared secret → primeiros 16 bytes = chave AES
- **Cifra:** AES-128-GCM (Galois/Counter Mode)
- **Nonce:** 12 bytes random per packet (`RandomNumberGenerator.Fill`)
- **Tag:** 16 bytes (128-bit autenticacao)

### 12.2 Encryption (Hardware)

```csharp
// ByteBuffer.Encrypt() — when AesGcm.IsSupported (.NET 5+)
public void Encrypt() {
    AesGcm aes = Connection.AesEncryptor;

    Span<byte> nonce = new Span<byte>(Data + Position, 12);
    RandomNumberGenerator.Fill(nonce);
    Position += 12;

    Span<byte> tag = new(Data + Position, 16);
    Position += 16;

    var cipher = new Span<byte>(Data + 1, Size - 1);  // Everything except the type byte
    aes.Encrypt(nonce, cipher, cipher, tag);           // In-place encryption

    Size = Position;
}
```

### 12.3 Encryption (Software Fallback)

```csharp
// ByteBuffer.EncryptSoftwareFallback() — BouncyCastle para Unity/plataformas sem AES-NI
var cipher = new GcmBlockCipher(new AesEngine());
var parameters = new AeadParameters(new KeyParameter(encryptionKey), 128, nonce);
cipher.Init(true, parameters);
cipher.ProcessBytes(plaintext, 0, plaintext.Length, ciphertext, 0);
cipher.DoFinal(ciphertext, offset);
```

### 12.4 Production State

**`#define ENCRYPT` was COMMENTED OUT.** The server ran without encryption, using only CRC32C for integrity. This means traffic was readable by any network sniffer.

