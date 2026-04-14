#ifndef NETUDP_CONFIG_H
#define NETUDP_CONFIG_H

/**
 * @file netudp_config.h
 * @brief Compile-time configuration for netudp.
 */

/* Compile flags (set via CMake or compiler defines):
 *
 * NETUDP_CRC32_ONLY      — Disable AEAD encryption, use CRC32C only (LAN/dev mode)
 * NETUDP_ENABLE_AVX512   — Enable AVX-512 SIMD path (experimental)
 */

/* Version */
#define NETUDP_VERSION_MAJOR 0
#define NETUDP_VERSION_MINOR 1
#define NETUDP_VERSION_PATCH 0
#define NETUDP_VERSION_STRING "NETUDP 0.01\0"

/* Limits */
#define NETUDP_MAX_CHANNELS          255
#define NETUDP_MAX_CLIENTS           65535
#define NETUDP_MTU                   1200
#define NETUDP_MAX_PACKET_ON_WIRE    1400
#define NETUDP_CONNECT_TOKEN_BYTES   2048
#define NETUDP_PRIVATE_KEY_BYTES     32
#define NETUDP_USER_DATA_BYTES       256
#define NETUDP_MAX_SERVERS_PER_TOKEN 32
#define NETUDP_MAX_MESSAGE_SIZE      (288 * 1024) /* 288 KB */

/* Version info for wire protocol (13 bytes, null-terminated) */
#define NETUDP_VERSION_INFO "NETUDP 1.01\0"
#define NETUDP_VERSION_INFO_BYTES 13

#endif /* NETUDP_CONFIG_H */
