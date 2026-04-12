#ifndef NETUDP_CONNECTION_H
#define NETUDP_CONNECTION_H

/**
 * @file connection.h
 * @brief Server-side connection state. One per connected client slot.
 */

#include <netudp/netudp_types.h>
#include "../crypto/packet_crypto.h"
#include "connect_token.h"

#include <cstdint>
#include <cstring>

namespace netudp {

struct Connection {
    bool     active = false;
    uint32_t generation = 0; /* Incremented on reuse to detect stale references */

    netudp_address_t address = {};
    uint64_t         client_id = 0;
    uint8_t          user_data[256] = {};

    crypto::KeyEpoch key_epoch;

    double   connect_time = 0.0;
    double   last_recv_time = 0.0;
    double   last_send_time = 0.0;
    uint32_t timeout_seconds = 10;

    void reset() {
        active = false;
        generation++;
        std::memset(&address, 0, sizeof(address));
        client_id = 0;
        std::memset(user_data, 0, sizeof(user_data));
        key_epoch = crypto::KeyEpoch{};
        connect_time = 0.0;
        last_recv_time = 0.0;
        last_send_time = 0.0;
        timeout_seconds = 10;
    }
};

} // namespace netudp

#endif /* NETUDP_CONNECTION_H */
