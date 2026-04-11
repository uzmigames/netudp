# Sequencing and Sliding Window

**Files:** `NetConstants.cs`, `Connection.cs`

### 8.1 Constants

```csharp
MaxSequence     = 8192    // Maximum sequence value
HalfMaxSequence = 4096    // Half for relative comparison
WindowSize      = 16384   // MaxSequence * 2
HalfWindowSize  = 8192    // Half of window
```

### 8.2 Sliding Window (Bit Array)

```csharp
int[] Window = new int[WindowSize / 32 + 1];  // 513 ints = 16416 bits

// Verificacao de duplicate e aceitacao:
if ((Window[sequence / 32] & (1 << (sequence % 32))) == 0) {
    // New packet — marks as received
    Window[sequence / 32] |= (1 << (sequence % 32));

    // Clears the opposite half of the window (allows sequence reuse)
    int index = Math.Abs((HalfWindowSize + sequence) % WindowSize);
    Window[index / 32] &= ~(1 << (index % 32));

    // Processa o pacote...
}
// If bit already set → duplicate → silently ignored
```

### 8.3 Relative Sequence Comparison

```csharp
// NetConstants.cs
public static int RelativeSequenceNumber(int number, int expected) {
    return (number - expected + MaxSequence + HalfMaxSequence) % MaxSequence - HalfMaxSequence;
}

// NetworkGeneral.cs (alternative version)
public static int SeqDiff(int a, int b) {
    return Diff(a, b, HalfMaxGameSequence);  // HalfMaxGameSequence = 16000
}
public static int Diff(int a, int b, int halfMax) {
    return (a - b + halfMax * 3) % (halfMax * 2) - halfMax;
}
```

