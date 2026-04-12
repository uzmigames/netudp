#ifndef NETUDP_FRAME_H
#define NETUDP_FRAME_H

/**
 * @file frame.h
 * @brief Wire format frame types and encoding/decoding (spec 09).
 *
 * Frame types inside encrypted payload (after AckFields):
 *   0x02 STOP_WAITING
 *   0x03 UNRELIABLE_DATA
 *   0x04 RELIABLE_DATA
 *   0x05 FRAGMENT_DATA
 *   0x06 DISCONNECT
 */

#include <cstdint>
#include <cstring>

namespace netudp {
namespace wire {

static constexpr uint8_t FRAME_STOP_WAITING    = 0x02;
static constexpr uint8_t FRAME_UNRELIABLE_DATA = 0x03;
static constexpr uint8_t FRAME_RELIABLE_DATA   = 0x04;
static constexpr uint8_t FRAME_FRAGMENT_DATA   = 0x05;
static constexpr uint8_t FRAME_DISCONNECT      = 0x06;

/**
 * Write an UNRELIABLE_DATA frame: type(1) + channel(1) + msg_len(2) + data(N)
 * Returns total bytes written.
 */
inline int write_unreliable_frame(uint8_t* buf, int buf_avail,
                                   uint8_t channel, const uint8_t* data, int data_len) {
    int needed = 1 + 1 + 2 + data_len;
    if (needed > buf_avail) {
        return -1;
    }
    buf[0] = FRAME_UNRELIABLE_DATA;
    buf[1] = channel;
    uint16_t len16 = static_cast<uint16_t>(data_len);
    std::memcpy(buf + 2, &len16, 2);
    std::memcpy(buf + 4, data, static_cast<size_t>(data_len));
    return needed;
}

/**
 * Write a RELIABLE_DATA frame: type(1) + channel(1) + msg_seq(2) + msg_len(2) + data(N)
 * Returns total bytes written.
 */
inline int write_reliable_frame(uint8_t* buf, int buf_avail,
                                 uint8_t channel, uint16_t msg_seq,
                                 const uint8_t* data, int data_len) {
    int needed = 1 + 1 + 2 + 2 + data_len;
    if (needed > buf_avail) {
        return -1;
    }
    buf[0] = FRAME_RELIABLE_DATA;
    buf[1] = channel;
    std::memcpy(buf + 2, &msg_seq, 2);
    uint16_t len16 = static_cast<uint16_t>(data_len);
    std::memcpy(buf + 4, &len16, 2);
    std::memcpy(buf + 6, data, static_cast<size_t>(data_len));
    return needed;
}

/**
 * Write a FRAGMENT_DATA frame: type(1) + channel(1) + msg_id(2) + frag_idx(1) + frag_cnt(1) + data(N)
 * Returns total bytes written.
 */
inline int write_fragment_frame(uint8_t* buf, int buf_avail,
                                 uint8_t channel, uint16_t msg_id,
                                 uint8_t frag_idx, uint8_t frag_cnt,
                                 const uint8_t* data, int data_len) {
    int needed = 1 + 1 + 2 + 1 + 1 + data_len;
    if (needed > buf_avail) {
        return -1;
    }
    buf[0] = FRAME_FRAGMENT_DATA;
    buf[1] = channel;
    std::memcpy(buf + 2, &msg_id, 2);
    buf[4] = frag_idx;
    buf[5] = frag_cnt;
    std::memcpy(buf + 6, data, static_cast<size_t>(data_len));
    return needed;
}

/**
 * Write a DISCONNECT frame: type(1) + reason(1)
 */
inline int write_disconnect_frame(uint8_t* buf, int buf_avail, uint8_t reason) {
    if (buf_avail < 2) {
        return -1;
    }
    buf[0] = FRAME_DISCONNECT;
    buf[1] = reason;
    return 2;
}

/** Parse frame type from buffer. Returns the frame type byte, or 0 on empty. */
inline uint8_t peek_frame_type(const uint8_t* buf, int avail) {
    if (avail < 1) {
        return 0;
    }
    return buf[0];
}

} // namespace wire
} // namespace netudp

#endif /* NETUDP_FRAME_H */
