#include "random.h"
#include "../core/platform.h"

#if defined(NETUDP_PLATFORM_WINDOWS)
#include <windows.h>
#include <bcrypt.h>
#elif defined(NETUDP_PLATFORM_MACOS)
#include <stdlib.h> /* arc4random_buf */
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace netudp {
namespace crypto {

void random_bytes(uint8_t* data, int len) {
    if (data == nullptr || len <= 0) {
        return;
    }

#if defined(NETUDP_PLATFORM_WINDOWS)
    BCryptGenRandom(nullptr, data, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG);

#elif defined(NETUDP_PLATFORM_MACOS)
    arc4random_buf(data, static_cast<size_t>(len));

#else /* Linux / other Unix */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t total = 0;
        while (total < len) {
            ssize_t n = read(fd, data + total, static_cast<size_t>(len - total));
            if (n <= 0) {
                break;
            }
            total += n;
        }
        close(fd);
    }
#endif
}

} // namespace crypto
} // namespace netudp
