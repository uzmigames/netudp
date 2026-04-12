#ifndef NETUDP_RING_BUFFER_H
#define NETUDP_RING_BUFFER_H

/**
 * @file ring_buffer.h
 * @brief Fixed-capacity circular buffer. Zero allocation after construction.
 *
 * N must be a power of 2 (enforced at compile time) for efficient modular indexing.
 */

#include <cstdint>
#include <type_traits>

namespace netudp {

template <typename T, int N>
class FixedRingBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0,
        "FixedRingBuffer capacity N must be a power of 2");

public:
    FixedRingBuffer() = default;

    bool push_back(const T& item) {
        if (size_ == N) {
            return false;
        }
        data_[(head_ + size_) & MASK] = item;
        ++size_;
        return true;
    }

    bool pop_front(T* out) {
        if (size_ == 0) {
            return false;
        }
        if (out != nullptr) {
            *out = data_[head_];
        }
        data_[head_] = T{};
        head_ = (head_ + 1) & MASK;
        --size_;
        return true;
    }

    T& operator[](int index) {
        return data_[(head_ + index) & MASK];
    }

    const T& operator[](int index) const {
        return data_[(head_ + index) & MASK];
    }

    T& front() { return data_[head_]; }
    const T& front() const { return data_[head_]; }

    T& back() { return data_[(head_ + size_ - 1) & MASK]; }
    const T& back() const { return data_[(head_ + size_ - 1) & MASK]; }

    /** Access by absolute sequence number (index = seq & MASK). */
    T& at_seq(int seq) { return data_[seq & MASK]; }
    const T& at_seq(int seq) const { return data_[seq & MASK]; }

    int  size() const { return size_; }
    int  capacity() const { return N; }
    bool full() const { return size_ == N; }
    bool is_empty() const { return size_ == 0; }

    void clear() {
        for (int i = 0; i < N; ++i) {
            data_[i] = T{};
        }
        head_ = 0;
        size_ = 0;
    }

private:
    static constexpr int MASK = N - 1;
    T   data_[N]{};
    int head_ = 0;
    int size_ = 0;
};

} // namespace netudp

#endif /* NETUDP_RING_BUFFER_H */
