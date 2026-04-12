#include "crc32c.h"
#include "../simd/netudp_simd.h"

namespace netudp {
namespace crypto {

uint32_t crc32c(const uint8_t* data, int len) {
    if (simd::g_simd != nullptr) {
        return simd::g_simd->crc32c(data, len);
    }
    return simd::g_ops_generic.crc32c(data, len);
}

} // namespace crypto
} // namespace netudp
