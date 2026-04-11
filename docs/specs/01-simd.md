# Spec 01 — SIMD Detection & Dispatch

## Requirements

### REQ-01.1: Runtime Detection
The system SHALL detect CPU SIMD capabilities at `netudp_init()` time.
On x86-64: SHALL use CPUID to detect SSE4.2, AVX2, AVX-512.
On ARM64: NEON SHALL be assumed as baseline (always available).

```cpp
enum class SimdLevel : uint8_t {
    Generic = 0,  // Scalar fallback (always compiled)
    SSE42   = 1,  // x86-64: SSE4.2 + POPCNT + CRC32
    AVX2    = 2,  // x86-64: AVX2 + BMI2 + FMA
    NEON    = 3,  // ARM64: NEON (baseline)
    AVX512  = 4,  // x86-64: AVX-512F (future)
};
```

### REQ-01.2: Dispatch Table
The system SHALL use a function pointer table, set once at init, for zero-overhead dispatch.

```cpp
struct SimdOps {
    // Integrity
    uint32_t (*crc32c)(const uint8_t* data, int len);

    // Memory
    void (*memcpy_nt)(void* dst, const void* src, size_t len);
    void (*memset_zero)(void* dst, size_t len);

    // Ack processing
    int  (*ack_bits_scan)(uint32_t bits, int* indices);  // Unacked packet indices
    int  (*popcount32)(uint32_t v);

    // Replay
    int  (*replay_check)(const uint64_t* window, uint64_t seq, int size);

    // Fragment
    int  (*fragment_bitmask_complete)(const uint8_t* mask, int total);
    int  (*fragment_next_missing)(const uint8_t* mask, int total);

    // Stats
    void (*accumulate_u64)(uint64_t* dst, const uint64_t* src, int count);

    // Address
    int  (*addr_equal)(const void* a, const void* b, int len);
};

extern const SimdOps* g_simd;  // Set at netudp_init(), never changes
```

### REQ-01.3: Per-ISA Compilation
Each ISA implementation SHALL be in a separate .cpp file compiled with its own flags:
- `simd_generic.cpp` — no flags
- `simd_sse42.cpp` — `-msse4.2 -mpopcnt`
- `simd_avx2.cpp` — `-mavx2 -mbmi2`
- `simd_neon.cpp` — ARM64 default
- `simd_avx512.cpp` — `-mavx512f` (future)

### REQ-01.4: Non-Temporal Stores
Buffer pool copy operations SHALL use non-temporal (streaming) stores on x86:
- SSE4.2: `_mm_stream_si128`
- AVX2: `_mm256_stream_si256`
- With `_mm_sfence()` after the store loop

This prevents L1/L2 cache pollution when copying packet data at high throughput.

### REQ-01.5: Public Query

```cpp
// extern "C"
netudp_simd_level_t netudp_simd_level(void);
// Returns the resolved SIMD level (never 0 after init)
```

## Scenarios

#### Scenario: AVX2 detection on modern Intel
Given a CPU with AVX2 support
When `netudp_init()` is called
Then `netudp_simd_level()` returns `NETUDP_SIMD_AVX2`
And `g_simd->crc32c` points to the SSE4.2 CRC32C implementation
And `g_simd->memcpy_nt` points to the AVX2 non-temporal copy

#### Scenario: Fallback on old CPU
Given a CPU without SSE4.2
When `netudp_init()` is called
Then `netudp_simd_level()` returns `NETUDP_SIMD_GENERIC`
And all operations use scalar fallback
And functionality is identical (just slower)

#### Scenario: ARM64 server
Given an ARM64 CPU (e.g., AWS Graviton)
When `netudp_init()` is called
Then `netudp_simd_level()` returns `NETUDP_SIMD_NEON`
And `g_simd->crc32c` uses ARM CRC32 instructions if available
