# Spec 03 — Zero-GC Memory & Pool System

## Requirements

### REQ-03.1: Custom Allocator Interface

```cpp
struct netudp_allocator_t {
    void* context;
    void* (*alloc)(void* context, size_t bytes);
    void  (*free)(void* context, void* ptr);
};
```

The allocator SHALL only be called during `create()`/`start()` and `stop()`/`destroy()`.
During runtime (`update()`, `send()`, `receive()`), zero allocator calls SHALL occur.

### REQ-03.2: Pool\<T\> — Fixed-Capacity Object Pool

```cpp
template<typename T>
class Pool {
public:
    Pool(uint32_t capacity, Allocator& alloc);
    ~Pool();

    T*       acquire();      // O(1) — pop from free list. Returns nullptr if exhausted.
    void     release(T* p);  // O(1) — push to free list.
    uint32_t count() const;  // Active items
    uint32_t capacity() const;
    bool     full() const;

private:
    T*        storage_;       // Single contiguous allocation
    uint32_t* free_stack_;    // Stack of free indices
    uint32_t  free_top_;
    uint32_t  capacity_;
};
```

Pool SHALL:
- Allocate `capacity * sizeof(T)` + `capacity * sizeof(uint32_t)` once in constructor
- Use a stack-based free list (not linked list) for cache-friendly sequential access
- Align storage to 64 bytes (cache line)
- Return `nullptr` on exhaustion (never fall back to heap)
- Zero-initialize released slots (security: prevent data leakage)

### REQ-03.3: FixedRingBuffer\<T, N\>

```cpp
template<typename T, size_t N>
class FixedRingBuffer {
    static_assert((N & (N-1)) == 0, "N must be power of 2");

    alignas(64) T data_[N];
    uint32_t head_ = 0;
    uint32_t tail_ = 0;

public:
    bool     push(const T& item);  // Returns false if full
    bool     pop(T& out);          // Returns false if empty
    bool     peek(T& out) const;
    uint32_t count() const;
    bool     empty() const;
    bool     full() const;
    void     clear();
    constexpr size_t capacity() const { return N; }
};
```

N MUST be a power of 2 (for bitmask modulo). Static assert enforced.

### REQ-03.4: FixedHashMap\<K, V, N\>

```cpp
template<typename K, typename V, size_t N>
class FixedHashMap {
    struct Entry {
        K key;
        V value;
        uint8_t state;  // 0=empty, 1=occupied, 2=tombstone
    };

    alignas(64) Entry entries_[N];
    uint32_t count_ = 0;

public:
    V*   find(const K& key);          // Returns nullptr if not found
    bool insert(const K& key, const V& value);  // Returns false if full
    bool remove(const K& key);
    uint32_t count() const;
    void clear();

    // Iteration
    template<typename Fn>
    void for_each(Fn&& fn);  // fn(const K& key, V& value)
};
```

Open addressing with linear probing. N should be ≥ 2× expected items for good load factor.
Hash function for Address: SIMD-accelerated byte comparison (`g_simd->addr_equal`).

### REQ-03.5: Memory Budget

```cpp
struct netudp_memory_budget_t {
    size_t packet_pool;      // max_clients * packets_per_client * MTU
    size_t message_pool;     // max_clients * messages_per_client * sizeof(Message)
    size_t fragment_pool;    // max_clients * max_fragments * MTU  (if fragmentation enabled)
    size_t connection_pool;  // max_clients * sizeof(Connection)
    size_t compression_pool; // max_clients * channels * sizeof(netc_ctx)  (if compression enabled)
    size_t total;
};

// Query predicted memory usage before starting
netudp_memory_budget_t netudp_calculate_memory_budget(const netudp_server_config_t* config);
```

### REQ-03.6: Zero-GC Verification

In debug builds, a global atomic counter SHALL track all allocations after `server_start()`.
If any allocation occurs during runtime, `NETUDP_ASSERT(!"Zero-GC violation")` SHALL fire.

```cpp
#ifdef NETUDP_DEBUG
inline std::atomic<int> g_runtime_alloc_count{0};
inline bool g_runtime_phase = false;

void* debug_alloc(void* ctx, size_t bytes) {
    if (g_runtime_phase) {
        NETUDP_ASSERT(!"Zero-GC violation: allocation during runtime");
    }
    g_runtime_alloc_count++;
    return original_alloc(ctx, bytes);
}
#endif
```

## Scenarios

#### Scenario: Pool acquire/release cycle
Given a `Pool<Packet>` with capacity 1024
When 1024 packets are acquired
Then `pool.full() == true` and `pool.acquire() == nullptr`
When 1 packet is released
Then `pool.acquire()` returns a valid pointer

#### Scenario: Memory budget prediction
Given config with max_clients=256, compression=false, fragmentation=false
When `netudp_calculate_memory_budget(&config)`
Then `budget.total <= 32 * 1024 * 1024` (32 MB)

#### Scenario: Zero-GC violation detection
Given a server is started (runtime phase active)
When code calls `new Packet()` inside `server_update()`
Then debug build asserts with "Zero-GC violation"
