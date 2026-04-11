# 7. CookieManager — Anti-DDoS

**File:** `Core/Network/Security/CookieManager.cs` (83 lines)

## Purpose

Stateless connection verification to prevent IP spoofing and amplification attacks. The server does NOT allocate any per-connection state until the cookie is validated.

**Server1 had no equivalent** — it allocated a DH connection entry immediately on first packet.

## Cookie Format (48 bytes)

```
Offset  Size  Field
0       8     Unix timestamp (seconds)
8       8     Random nonce
16      32    HMAC-SHA256 signature
```

## Generation

```csharp
public static byte[] GenerateCookie(Address clientAddress) {
    var timestamp = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
    var random = new byte[8];
    RandomNumberGenerator.Fill(random);

    // Data to sign: client_address_bytes + timestamp + random
    var data = new List<byte>();
    data.AddRange(clientAddressBytes);  // Full address struct
    data.AddRange(BitConverter.GetBytes(timestamp));
    data.AddRange(random);

    using var hmac = new HMACSHA256(ServerSecret);  // 32-byte random server secret
    var signature = hmac.ComputeHash(data.ToArray());

    // Cookie = timestamp (8) + random (8) + signature (32) = 48 bytes
    var cookie = new byte[48];
    BitConverter.GetBytes(timestamp).CopyTo(cookie, 0);
    random.CopyTo(cookie, 8);
    signature.CopyTo(cookie, 16);
    return cookie;
}
```

## Validation

```csharp
public static bool ValidateCookie(byte[] cookie, Address clientAddress) {
    if (cookie == null || cookie.Length != 48) return false;

    // 1. Check TTL (10 seconds)
    var timestamp = BitConverter.ToInt64(cookie, 0);
    if (DateTimeOffset.UtcNow - DateTimeOffset.FromUnixTimeSeconds(timestamp) > CookieTTL)
        return false;

    // 2. Recompute HMAC and compare
    var data = clientAddressBytes + timestamp + random;
    var expectedSignature = HMACSHA256(ServerSecret, data);
    return providedSignature.SequenceEqual(expectedSignature);
}
```

## Anti-DDoS Properties

1. **Stateless** — server stores nothing until cookie validated. An attacker cannot exhaust server memory.
2. **IP-bound** — cookie includes client address, so spoofed IPs produce invalid cookies.
3. **Time-limited** — 10-second TTL prevents cookie replay.
4. **Cryptographically signed** — cannot be forged without server secret.

## Connection Flow

```
Attacker (spoofed IP)          Server
  │ Connect                      │
  │ ────────────────────────────→│ No state allocated!
  │                              │ Generate cookie, send to spoofed IP
  │ Cookie → goes to real IP     │ Attacker never receives it
  │                              │ → Connection never established
```
