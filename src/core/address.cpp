#include <netudp/netudp.h>
#include "address.h"
#include "../simd/netudp_simd.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {

int netudp_parse_address(const char* str, netudp_address_t* addr) {
    if (str == nullptr || addr == nullptr) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    *addr = netudp::address_zero();

    /* IPv6: "[::1]:port" or "[2001:db8::1]:port" */
    if (str[0] == '[') {
        const char* close = std::strchr(str, ']');
        if (close == nullptr) {
            return NETUDP_ERROR_INVALID_PARAM;
        }

        int ipv6_len = static_cast<int>(close - str - 1);
        if (ipv6_len <= 0 || ipv6_len > 45) {
            return NETUDP_ERROR_INVALID_PARAM;
        }

        char ipv6_str[46] = {};
        std::memcpy(ipv6_str, str + 1, static_cast<size_t>(ipv6_len));

        /* Parse port after "]:port" */
        if (close[1] != ':') {
            return NETUDP_ERROR_INVALID_PARAM;
        }
        int port = std::atoi(close + 2);
        if (port <= 0 || port > 65535) {
            return NETUDP_ERROR_INVALID_PARAM;
        }

        /* Parse IPv6 address — simplified parser for common formats */
        /* Supports: "::", "::1", "2001:db8::1", full 8-group notation */
        uint16_t groups[8] = {};
        int group_count = 0;
        int double_colon_pos = -1;
        const char* p = ipv6_str;

        while (*p != '\0' && group_count < 8) {
            if (p[0] == ':' && p[1] == ':') {
                if (double_colon_pos >= 0) {
                    return NETUDP_ERROR_INVALID_PARAM; /* Only one :: allowed */
                }
                double_colon_pos = group_count;
                p += 2;
                if (*p == '\0') {
                    break;
                }
                continue;
            }
            if (*p == ':') {
                ++p;
            }

            char* end = nullptr;
            unsigned long val = std::strtoul(p, &end, 16);
            if (end == p || val > 0xFFFF) {
                return NETUDP_ERROR_INVALID_PARAM;
            }
            groups[group_count++] = static_cast<uint16_t>(val);
            p = end;
        }

        /* Expand :: into zeros */
        if (double_colon_pos >= 0) {
            int fill = 8 - group_count;
            if (fill < 0) {
                return NETUDP_ERROR_INVALID_PARAM;
            }
            /* Shift groups after :: to the right */
            for (int i = group_count - 1; i >= double_colon_pos; --i) {
                groups[i + fill] = groups[i];
            }
            for (int i = 0; i < fill; ++i) {
                groups[double_colon_pos + i] = 0;
            }
        }

        addr->type = NETUDP_ADDRESS_IPV6;
        addr->port = static_cast<uint16_t>(port);
        for (int i = 0; i < 8; ++i) {
            addr->data.ipv6[i] = groups[i];
        }
        return NETUDP_OK;
    }

    /* IPv4: "1.2.3.4:port" or "hostname:port" (only numeric IPs for now) */
    const char* colon = std::strrchr(str, ':');
    if (colon == nullptr) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    int port = std::atoi(colon + 1);
    if (port <= 0 || port > 65535) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    int ip_len = static_cast<int>(colon - str);
    if (ip_len <= 0 || ip_len > 45) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    char ip_str[46] = {};
    std::memcpy(ip_str, str, static_cast<size_t>(ip_len));

    /* Handle special cases */
    if (std::strcmp(ip_str, "0.0.0.0") == 0 || std::strcmp(ip_str, "::") == 0) {
        if (std::strcmp(ip_str, "::") == 0) {
            addr->type = NETUDP_ADDRESS_IPV6;
        } else {
            addr->type = NETUDP_ADDRESS_IPV4;
        }
        addr->port = static_cast<uint16_t>(port);
        return NETUDP_OK;
    }

    /* Parse dotted-decimal IPv4 */
    unsigned int a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        if (a > 255 || b > 255 || c > 255 || d > 255) {
            return NETUDP_ERROR_INVALID_PARAM;
        }
        addr->type = NETUDP_ADDRESS_IPV4;
        addr->port = static_cast<uint16_t>(port);
        addr->data.ipv4[0] = static_cast<uint8_t>(a);
        addr->data.ipv4[1] = static_cast<uint8_t>(b);
        addr->data.ipv4[2] = static_cast<uint8_t>(c);
        addr->data.ipv4[3] = static_cast<uint8_t>(d);
        return NETUDP_OK;
    }

    return NETUDP_ERROR_INVALID_PARAM;
}

char* netudp_address_to_string(const netudp_address_t* addr, char* buf, int buf_len) {
    if (addr == nullptr || buf == nullptr || buf_len < 2) {
        return buf;
    }

    if (addr->type == NETUDP_ADDRESS_IPV4) {
        std::snprintf(buf, static_cast<size_t>(buf_len), "%u.%u.%u.%u:%u",
            addr->data.ipv4[0], addr->data.ipv4[1],
            addr->data.ipv4[2], addr->data.ipv4[3],
            addr->port);
    } else if (addr->type == NETUDP_ADDRESS_IPV6) {
        /* Simplified: print all 8 groups, no :: compression */
        std::snprintf(buf, static_cast<size_t>(buf_len),
            "[%x:%x:%x:%x:%x:%x:%x:%x]:%u",
            addr->data.ipv6[0], addr->data.ipv6[1],
            addr->data.ipv6[2], addr->data.ipv6[3],
            addr->data.ipv6[4], addr->data.ipv6[5],
            addr->data.ipv6[6], addr->data.ipv6[7],
            addr->port);
    } else {
        std::snprintf(buf, static_cast<size_t>(buf_len), "none:0");
    }

    return buf;
}

int netudp_address_equal(const netudp_address_t* a, const netudp_address_t* b) {
    if (a == nullptr || b == nullptr) {
        return 0;
    }
    if (a->type != b->type || a->port != b->port) {
        return 0;
    }

    int data_len = netudp::address_data_len(a);
    if (data_len == 0) {
        return a->type == b->type ? 1 : 0;
    }

    if (netudp::simd::g_simd != nullptr) {
        return netudp::simd::g_simd->addr_equal(&a->data, &b->data, data_len);
    }
    return std::memcmp(&a->data, &b->data, static_cast<size_t>(data_len)) == 0 ? 1 : 0;
}

} /* extern "C" */
