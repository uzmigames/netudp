# Authentication (JWT)

**File:** `GameServer/Program.cs` (lines 85-103)

```csharp
NetManager.ConnectionRequestEvent = async (accept, reject, token) => {
    CharacterClaims claims = new JwtBuilder()
        .WithAlgorithm(new HMACSHA256Algorithm())
        .WithSecret("TOS_JWT_ENTER_SECRET_JWT_VALUE")
        .Decode<CharacterClaims>(token);

    Scene scene = SceneManager.GetOrCreate(claims.MapId, claims.MapName);
    Connection conn = accept(scene.Manager);

    // Cria ou recupera personagem, inicializa PlayerController...
};
```

**Claims do JWT:**
- `CharacterId` — ObjectId do MongoDB
- `MapId` — ID do mapa
- `MapName` — Nome do mapa (ex: "Forest")
- `Admin` — Flag de admin
- `Uzmi` — Flag especial

**Security note:** The secret key `"TOS_JWT_ENTER_SECRET_JWT_VALUE"` was hardcoded in the code. In netudp, authentication will be via opaque connect tokens.

