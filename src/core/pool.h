#ifndef NETUDP_POOL_H
#define NETUDP_POOL_H

/**
 * @file pool.h
 * @brief Zero-GC object pool with O(1) acquire/release.
 *
 * Pre-allocates N elements contiguously. Uses an intrusive free-list
 * (stores next-pointer in freed element memory). Zero allocations
 * after init — acquire/release are pointer swaps.
 */

#include "allocator.h"
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace netudp {

template <typename T>
class Pool {
    static_assert(sizeof(T) >= sizeof(void*),
        "Pool element must be at least pointer-sized for intrusive free-list");

public:
    Pool() = default;

    bool init(int capacity, const Allocator& alloc = default_allocator()) {
        if (capacity <= 0 || storage_ != nullptr) {
            return false;
        }

        allocator_ = alloc;
        capacity_ = capacity;
        available_ = capacity;

        storage_ = static_cast<T*>(allocator_.allocate(sizeof(T) * static_cast<size_t>(capacity)));
        if (storage_ == nullptr) {
            return false;
        }

        /* Zero-initialize all elements */
        std::memset(storage_, 0, sizeof(T) * static_cast<size_t>(capacity));

        /* Build free-list: each element's first pointer-sized bytes store next pointer */
        constexpr size_t PTR_SIZE = sizeof(void*);
        free_head_ = storage_;
        for (int i = 0; i < capacity - 1; ++i) {
            void* next = &storage_[i + 1];
            std::memcpy(&storage_[i], &next, PTR_SIZE);
        }
        /* Last element points to null */
        void* null_ptr = nullptr;
        std::memcpy(&storage_[capacity - 1], &null_ptr, PTR_SIZE);

        return true;
    }

    void destroy() {
        if (storage_ != nullptr) {
            allocator_.deallocate(storage_);
            storage_ = nullptr;
            free_head_ = nullptr;
            capacity_ = 0;
            available_ = 0;
        }
    }

    ~Pool() {
        destroy();
    }

    /* Non-copyable */
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    /* Movable */
    Pool(Pool&& other) noexcept
        : storage_(other.storage_)
        , free_head_(other.free_head_)
        , capacity_(other.capacity_)
        , available_(other.available_)
        , allocator_(other.allocator_) {
        other.storage_ = nullptr;
        other.free_head_ = nullptr;
        other.capacity_ = 0;
        other.available_ = 0;
    }

    Pool& operator=(Pool&& other) noexcept {
        if (this != &other) {
            destroy();
            storage_ = other.storage_;
            free_head_ = other.free_head_;
            capacity_ = other.capacity_;
            available_ = other.available_;
            allocator_ = other.allocator_;
            other.storage_ = nullptr;
            other.free_head_ = nullptr;
            other.capacity_ = 0;
            other.available_ = 0;
        }
        return *this;
    }

    /** Acquire an element from the pool. Returns nullptr if empty. O(1). */
    T* acquire() {
        if (free_head_ == nullptr) {
            return nullptr;
        }

        T* element = free_head_;

        /* Read next pointer from the element's memory */
        constexpr size_t PTR_SZ = sizeof(void*);
        void* next = nullptr;
        std::memcpy(&next, element, PTR_SZ);
        free_head_ = static_cast<T*>(next);

        /* Zero the element before returning */
        std::memset(element, 0, sizeof(T));

        --available_;
        return element;
    }

    /** Release an element back to the pool. O(1). */
    void release(T* element) {
        if (element == nullptr) {
            return;
        }

        /* Store current head in element's memory */
        void* head_ptr = free_head_;
        std::memcpy(element, &head_ptr, sizeof(void*));
        free_head_ = element;
        ++available_;
    }

    int capacity() const { return capacity_; }
    int available() const { return available_; }
    int in_use() const { return capacity_ - available_; }
    bool empty() const { return available_ == 0; }

private:
    T* storage_ = nullptr;
    T* free_head_ = nullptr;
    int capacity_ = 0;
    int available_ = 0;
    Allocator allocator_{};
};

} // namespace netudp

#endif /* NETUDP_POOL_H */
