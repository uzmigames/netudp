#ifndef NETUDP_CRC32C_H
#define NETUDP_CRC32C_H

#include <cstdint>

namespace netudp {
namespace crypto {

/** CRC32C checksum using SIMD dispatch (SSE4.2/ARM CRC or software table). */
uint32_t crc32c(const uint8_t* data, int len);

} // namespace crypto
} // namespace netudp

#endif /* NETUDP_CRC32C_H */
