# 2. Algorithms

## Multi-Codec Competition

netc tries multiple algorithms per packet and picks the smallest output:

```
Input packet
    │
    ├──→ tANS 12-bit (4096-entry tables)
    ├──→ tANS 10-bit (1024-entry tables, for small packets)
    ├──→ tANS-PCTX (per-position context-adaptive)
    ├──→ LZP (Lempel-Ziv Prediction)
    ├──→ LZ77 (back-reference compression)
    ├──→ RLE (run-length encoding)
    ├──→ Delta + any of above (inter-packet prediction)
    └──→ Passthrough (if all expand)
    │
    ▼
Smallest output wins
```

## Algorithm Details

### tANS (Finite State Entropy)
Primary codec. Branch-free table-driven decode. Fractional-bit precision (unlike Huffman which rounds to whole bits). Two table sizes:
- **12-bit** (4096 entries): better ratio for larger packets
- **10-bit** (1024 entries): better for ≤128B packets (less L1 cache pollution)

### LZP (Lempel-Ziv Prediction)
Hash-context byte prediction. Hashes 3 previous bytes, looks up prediction table. Matches cost ~1 bit, misses cost ~9 bits. Extremely effective for structured game packets with repeating field patterns.

### Delta Prediction
Inter-packet prediction using previous packet as reference:
- **XOR** for flags, floats (changed bits → mostly zeros)
- **Subtraction** for counters (small delta values)
- **Order-2 linear extrapolation** for smooth trends (position updates)

Only available in stateful mode. Field-class aware — different prediction per byte offset.

### Bigram-PCTX (Per-Position Context-Adaptive tANS)
Switches probability tables based on byte position AND previous byte's class. Captures structural patterns like "byte 0 is always a packet type, byte 1-4 is always an entity ID."

### Adaptive Mode
When enabled, frequency tables evolve per-connection:
- Accumulates byte frequency statistics across packets
- Periodically rebuilds tANS tables to match observed traffic
- Both encoder AND decoder update in lockstep (deterministic)
- ~16 KB memory overhead per context

### LZ77 Back-References
Standard LZ77 with 1-256 byte window (within-packet) or 1-65536 (cross-packet with ring buffer). Used when repetitive structure is detected.

## Compression Levels

```
0 = fastest (skip most competition passes)
5 = default (balanced)
9 = best ratio (try all codecs, all variants)
```

`NETC_CFG_FLAG_FAST_COMPRESS` skips expensive trial passes: 1.5-2.3x throughput gain, 2-5% ratio regression.
