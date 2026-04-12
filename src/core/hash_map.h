#ifndef NETUDP_HASH_MAP_H
#define NETUDP_HASH_MAP_H

/**
 * @file hash_map.h
 * @brief Fixed-capacity hash map with open addressing (linear probing).
 *        Zero allocation after construction. Designed for Address keys.
 */

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace netudp {

/** Default hash: FNV-1a over raw bytes. */
struct FNV1aHash {
    static uint32_t hash(const void* data, int len) {
        auto* bytes = static_cast<const uint8_t*>(data);
        uint32_t h = 2166136261U;
        for (int i = 0; i < len; ++i) {
            h ^= bytes[i];
            h *= 16777619U;
        }
        return h;
    }
};

template <typename K, typename V, int N, typename Hash = FNV1aHash>
class FixedHashMap {
    static_assert(N > 0 && (N & (N - 1)) == 0,
        "FixedHashMap capacity N must be a power of 2");

    struct Slot {
        K key{};
        V value{};
        bool occupied = false;
    };

public:
    FixedHashMap() = default;

    /** Insert or update. Returns pointer to value, or nullptr if full. */
    V* insert(const K& key, const V& value) {
        uint32_t h = Hash::hash(&key, sizeof(K));
        int idx = static_cast<int>(h & MASK);

        for (int probe = 0; probe < N; ++probe) {
            int slot = (idx + probe) & MASK;
            if (!slots_[slot].occupied) {
                slots_[slot].key = key;
                slots_[slot].value = value;
                slots_[slot].occupied = true;
                ++size_;
                return &slots_[slot].value;
            }
            if (std::memcmp(&slots_[slot].key, &key, sizeof(K)) == 0) {
                slots_[slot].value = value;
                return &slots_[slot].value;
            }
        }
        return nullptr; /* Full */
    }

    /** Find by key. Returns pointer to value, or nullptr if not found. */
    V* find(const K& key) {
        uint32_t h = Hash::hash(&key, sizeof(K));
        int idx = static_cast<int>(h & MASK);

        for (int probe = 0; probe < N; ++probe) {
            int slot = (idx + probe) & MASK;
            if (!slots_[slot].occupied) {
                return nullptr;
            }
            if (std::memcmp(&slots_[slot].key, &key, sizeof(K)) == 0) {
                return &slots_[slot].value;
            }
        }
        return nullptr;
    }

    const V* find(const K& key) const {
        return const_cast<FixedHashMap*>(this)->find(key);
    }

    /** Remove by key. Returns true if found and removed. */
    bool remove(const K& key) {
        uint32_t h = Hash::hash(&key, sizeof(K));
        int idx = static_cast<int>(h & MASK);

        for (int probe = 0; probe < N; ++probe) {
            int slot = (idx + probe) & MASK;
            if (!slots_[slot].occupied) {
                return false;
            }
            if (std::memcmp(&slots_[slot].key, &key, sizeof(K)) == 0) {
                /* Remove and re-hash subsequent entries (Robin Hood deletion) */
                slots_[slot].occupied = false;
                slots_[slot].key = K{};
                slots_[slot].value = V{};
                --size_;
                rehash_after_delete(slot);
                return true;
            }
        }
        return false;
    }

    int  size() const { return size_; }
    int  capacity() const { return N; }
    bool is_empty() const { return size_ == 0; }
    bool full() const { return size_ == N; }

    void clear() {
        for (int i = 0; i < N; ++i) {
            slots_[i] = Slot{};
        }
        size_ = 0;
    }

    /** Iterate all occupied entries. Callback: bool fn(const K&, V&) — return false to stop. */
    template <typename Fn>
    void for_each(Fn fn) {
        for (int i = 0; i < N; ++i) {
            if (slots_[i].occupied) {
                if (!fn(slots_[i].key, slots_[i].value)) {
                    return;
                }
            }
        }
    }

private:
    static constexpr int MASK = N - 1;

    void rehash_after_delete(int deleted_slot) {
        int slot = (deleted_slot + 1) & MASK;
        while (slots_[slot].occupied) {
            K key = slots_[slot].key;
            V value = slots_[slot].value;
            slots_[slot].occupied = false;
            --size_;
            insert(key, value);
            slot = (slot + 1) & MASK;
        }
    }

    Slot slots_[N]{};
    int size_ = 0;
};

} // namespace netudp

#endif /* NETUDP_HASH_MAP_H */
