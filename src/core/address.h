#ifndef NETUDP_ADDRESS_H
#define NETUDP_ADDRESS_H

/**
 * @file address.h
 * @brief Internal address utilities (parsing, formatting, comparison, hashing).
 */

#include <netudp/netudp_types.h>
#include <cstdint>
#include <cstring>

namespace netudp {

/** Zero-initialize an address (ensures union padding is clean for hashing). */
inline netudp_address_t address_zero() {
    netudp_address_t addr;
    std::memset(&addr, 0, sizeof(addr));
    return addr;
}

/** Number of address bytes relevant to the type (4 for IPv4, 16 for IPv6). */
inline int address_data_len(const netudp_address_t* addr) {
    if (addr->type == NETUDP_ADDRESS_IPV4) {
        return 4;
    }
    if (addr->type == NETUDP_ADDRESS_IPV6) {
        return 16;
    }
    return 0;
}

/** FNV-1a hash over type-relevant bytes + port. For FixedHashMap keys. */
inline uint32_t address_hash(const netudp_address_t* addr) {
    uint32_t h = 2166136261U;
    auto mix = [&h](uint8_t byte) {
        h ^= byte;
        h *= 16777619U;
    };

    mix(addr->type);

    int data_len = address_data_len(addr);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&addr->data);
    for (int i = 0; i < data_len; ++i) {
        mix(bytes[i]);
    }

    mix(static_cast<uint8_t>(addr->port & 0xFF));
    mix(static_cast<uint8_t>((addr->port >> 8) & 0xFF));

    return h;
}

} // namespace netudp

#endif /* NETUDP_ADDRESS_H */
