# Symbol Table (Per-Connection String Interning)

**File:** `ByteBuffer.cs` (lines 860-912)

### 21.1 Concept

Repeated strings (skill names, items, etc.) are sent once. Afterwards, only the numeric index is transmitted.

### 21.2 Implementation

```csharp
// Envio:
public void PutSymbol(string symbol, int maxLength = 80) {
    if (Connection.SymbolToIndex.TryGetValue(symbol, out uint index)) {
        PutVar(index);  // Already known → sends only index (1-4 bytes)
    } else {
        index = ++Connection.SymbolPool;
        Connection.SymbolToIndex.Add(symbol, index);
        Connection.IndexToSymbol.SetAt((int)index, symbol);

        PutVar(index);        // New index
        Put(symbol, maxLength); // + full string (first time)
    }
}

// Recepcao:
public string GetSymbol(int maxLength = 80) {
    uint index = GetVarUInt();
    if (index == 0) return "";

    if (Connection.IndexToSymbolRemote.Size > index) {
        return Connection.IndexToSymbolRemote.Values[index];  // Cache hit
    } else {
        string symbol = GetString(maxLength);  // First time → reads string
        Connection.IndexToSymbolRemote.SetAt((int)index, symbol);
        return symbol;
    }
}
```

### 21.3 Savings

- Skill "Fireball" (8 chars = 10 bytes with length prefix)
- First time: 1-2 bytes (index) + 10 bytes = ~12 bytes
- Subsequent: 1-2 bytes (index) only
- Savings per repeated use: **~83%** reduction

**Separate tables for send/receive** — each direction has its own mapping.

