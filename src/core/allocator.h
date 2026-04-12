#ifndef NETUDP_ALLOCATOR_H
#define NETUDP_ALLOCATOR_H

/**
 * @file allocator.h
 * @brief Custom allocator interface for netudp.
 *
 * Only called during Phase 1 (create/start) and Phase 3 (stop/destroy).
 * During runtime (Phase 2), all allocation goes through internal pools.
 */

#include <cstddef>
#include <cstdlib>

namespace netudp {

struct Allocator {
    void* context = nullptr;
    void* (*alloc)(void* ctx, size_t bytes) = nullptr;
    void  (*free)(void* ctx, void* ptr) = nullptr;

    void* allocate(size_t bytes) const {
        if (alloc != nullptr) {
            return alloc(context, bytes);
        }
        return std::malloc(bytes);
    }

    void deallocate(void* ptr) const {
        if (free != nullptr) {
            free(context, ptr);
            return;
        }
        std::free(ptr);
    }
};

inline Allocator default_allocator() {
    return Allocator{nullptr, nullptr, nullptr};
}

} // namespace netudp

#endif /* NETUDP_ALLOCATOR_H */
