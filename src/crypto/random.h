#ifndef NETUDP_RANDOM_H
#define NETUDP_RANDOM_H

#include <cstdint>

namespace netudp {
namespace crypto {

/** Fill buffer with cryptographically secure random bytes. */
void random_bytes(uint8_t* data, int len);

} // namespace crypto
} // namespace netudp

#endif /* NETUDP_RANDOM_H */
