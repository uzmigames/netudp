# Handshake Protocol

**Files:** `NetManager.cs` (lines 126-285), `Connection.DiffieHellman.cs` (87 lines)

### 4.1 Complete Flow (4 steps)

```
Cliente                                          Servidor
   │                                                │
   │  1. DiffieHellmanKey                           │
   │     [type=8][client_pubkey_X][client_pubkey_Y] │
   │ ──────────────────────────────────────────────→ │
   │                                                │
   │     Servidor: InitializeDiffieHellman()        │
   │     Servidor: SetBobPoints(x, y)               │
   │     Servidor: GetPublicKey()                    │
   │                                                │
   │  2. DiffieHellmanResponseKey                   │
   │     [type=6][server_pubkey_X][server_pubkey_Y] │
   │ ←────────────────────────────────────────────── │
   │                                                │
   │     Cliente: SetBobPoints(x, y)                │
   │     Cliente: SwapEncryptionKey()               │
   │     → ECDH shared secret → SHA-256 → AES key  │
   │                                                │
   │  3. ConnectRequest (RELIABLE)                  │
   │     [type=9][JWT token string]                 │
   │ ──────────────────────────────────────────────→ │
   │                                                │
   │     Servidor: SwapEncryptionKey()              │
   │     Servidor: Decode JWT → CharacterClaims     │
   │     Servidor: ConnectionRequestEvent.Invoke()  │
   │     → accept(scene.Manager) ou reject()        │
   │                                                │
   │  4a. ACK (se aceito)                           │
   │     [type=3][sequence=1]                       │
   │ ←────────────────────────────────────────────── │
   │                                                │
   │  4b. ConnectionDenied (se rejeitado)           │
   │     [type=5]                                   │
   │ ←────────────────────────────────────────────── │
```

### 4.2 Key Exchange (ECDH)

```csharp
// Connection.DiffieHellman.cs
X9ECParameters x9EC = NistNamedCurves.GetByName("P-521");  // NIST P-521 curve
ECDomainParameters ecDomain = new ECDomainParameters(x9EC.Curve, x9EC.G, x9EC.N, x9EC.H, x9EC.GetSeed());

// Generates key pair:
ECKeyPairGenerator g = GeneratorUtilities.GetKeyPairGenerator("ECDH");
g.Init(new ECKeyGenerationParameters(ecDomain, new SecureRandom()));
AsymmetricCipherKeyPair keyPair = g.GenerateKeyPair();

// Derives symmetric key:
IBasicAgreement aKeyAgree = AgreementUtilities.GetBasicAgreement("ECDH");
aKeyAgree.Init(alicePrivateKey);
BigInteger sharedSecret = aKeyAgree.CalculateAgreement(bobPublicKey);

IDigest digest = new Sha256Digest();
byte[] symmetricKey = new byte[32];  // SHA-256 of shared secret
digest.BlockUpdate(sharedSecretBytes, 0, sharedSecretBytes.Length);
digest.DoFinal(symmetricKey, 0);

// Uses first 16 bytes as AES-128 key:
EncryptionKey = symmetricKey[0..16];
AesEncryptor = new AesGcm(EncryptionKey);
```

### 4.3 DH Connection Protection

- DH connections have timeout of **120 segundos**
- Cleanup every **5 segundos** in the receive thread
- `lock(DiffieHellmanLock)` protects concurrent access
- DH connection removed when promoted to active connection

