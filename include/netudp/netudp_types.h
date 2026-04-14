#ifndef NETUDP_TYPES_H
#define NETUDP_TYPES_H

/**
 * @file netudp_types.h
 * @brief Public types, error codes, and constants for netudp.
 *        All types are POD structs suitable for FFI.
 */

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --- Error codes --- */

#define NETUDP_OK                     (0)
#define NETUDP_ERROR_INVALID_PARAM   (-1)
#define NETUDP_ERROR_SOCKET          (-2)
#define NETUDP_ERROR_NO_BUFFERS      (-3)
#define NETUDP_ERROR_CONNECTION_FULL (-4)
#define NETUDP_ERROR_NOT_CONNECTED   (-5)
#define NETUDP_ERROR_MSG_TOO_LARGE   (-6)
#define NETUDP_ERROR_CRYPTO          (-7)
#define NETUDP_ERROR_TIMEOUT         (-8)
#define NETUDP_ERROR_WINDOW_FULL     (-9)
#define NETUDP_ERROR_COMPRESSION    (-10)
#define NETUDP_ERROR_NOT_INITIALIZED (-11)

/* --- Send flags --- */

#define NETUDP_SEND_UNRELIABLE  0
#define NETUDP_SEND_RELIABLE    1
#define NETUDP_SEND_NO_NAGLE    2
#define NETUDP_SEND_NO_DELAY    4  /* NO_NAGLE + immediate flush */

/* --- SIMD levels --- */

typedef enum {
    NETUDP_SIMD_GENERIC = 0,
    NETUDP_SIMD_SSE42   = 1,
    NETUDP_SIMD_AVX2    = 2,
    NETUDP_SIMD_NEON    = 3,
    NETUDP_SIMD_AVX512  = 4
} netudp_simd_level_t;

/* --- Address --- */

typedef enum {
    NETUDP_ADDRESS_NONE = 0,
    NETUDP_ADDRESS_IPV4 = 1,
    NETUDP_ADDRESS_IPV6 = 2
} netudp_address_type_t;

typedef struct netudp_address {
    union {
        uint8_t  ipv4[4];
        uint16_t ipv6[8];
    } data;
    uint16_t port;
    uint8_t  type;   /* netudp_address_type_t */
    uint8_t  _pad;   /* padding for alignment */
} netudp_address_t;

/* --- Opaque handles --- */

typedef struct netudp_server netudp_server_t;
typedef struct netudp_client netudp_client_t;
typedef struct netudp_buffer netudp_buffer_t;

/* --- Message (returned by receive) --- */

typedef struct netudp_message {
    void*    data;
    int      size;
    int      channel;
    int      client_index;
    int      flags;
    int64_t  message_number;
    uint64_t receive_time_us;
} netudp_message_t;

/* --- Callbacks --- */

typedef void (*netudp_connect_fn)(void* ctx, int client_index, uint64_t client_id,
                                   const uint8_t user_data[256]);
typedef void (*netudp_disconnect_fn)(void* ctx, int client_index, int reason);
typedef void (*netudp_packet_handler_fn)(void* ctx, int client_index,
                                          const void* data, int size, int channel);

/* --- Crypto mode --- */

typedef enum {
    NETUDP_CRYPTO_AUTO      = 0, /**< Auto-detect: AES-GCM if AES-NI available, else XChaCha20 (default) */
    NETUDP_CRYPTO_AES_GCM   = 1, /**< AES-256-GCM (requires AES-NI, ~4x faster with cached BCrypt handles) */
    NETUDP_CRYPTO_XCHACHA20 = 2  /**< XChaCha20-Poly1305 (nonce-misuse resistant, software fallback) */
} netudp_crypto_mode_t;

/* --- Channel config --- */

typedef enum {
    NETUDP_CHANNEL_UNRELIABLE          = 0,
    NETUDP_CHANNEL_UNRELIABLE_SEQUENCED = 1,
    NETUDP_CHANNEL_RELIABLE_ORDERED    = 2,
    NETUDP_CHANNEL_RELIABLE_UNORDERED  = 3
} netudp_channel_type_t;

typedef struct netudp_channel_config {
    uint8_t  type;        /* netudp_channel_type_t */
    uint8_t  priority;    /* 0-255, higher = more important */
    uint8_t  weight;      /* relative weight within same priority */
    uint8_t  compression; /* 0=none, 1=stateful, 2=stateless */
    uint16_t nagle_ms;    /* Nagle timer in milliseconds (0 = disabled) */
    uint16_t _reserved;
    int      max_message_size; /* 0 = no fragmentation support */
} netudp_channel_config_t;

/* --- Server config --- */

typedef struct netudp_server_config {
    uint64_t protocol_id;
    uint8_t  private_key[32];

    /* Memory */
    void*  allocator_context;
    void*  (*allocate_function)(void* ctx, size_t bytes);
    void   (*free_function)(void* ctx, void* ptr);

    /* Callbacks */
    void*  callback_context;
    netudp_connect_fn    on_connect;
    netudp_disconnect_fn on_disconnect;

    /* Channels */
    netudp_channel_config_t channels[255];
    int num_channels;

    /* Compression (optional) */
    const void* compression_dict; /* netc_dict_t*, NULL = no compression */
    uint8_t     compression_level;

    /* Network simulator (optional, NULL disables) */
    const void* sim_config; /* NetSimConfig*, NULL = disabled */

    /* Crypto mode */
    uint8_t crypto_mode; /**< netudp_crypto_mode_t. Default: NETUDP_CRYPTO_AUTO (AES-GCM if AES-NI, else XChaCha20). */

    /* Threading (Linux only — SO_REUSEPORT multi-thread I/O) */
    int num_io_threads; /**< Number of I/O threads. 0 or 1 = single-threaded (default). */

    /* Logging */
    int  log_level;
    void (*log_callback)(int level, const char* msg);
} netudp_server_config_t;

/* --- Client config --- */

typedef struct netudp_client_config {
    uint64_t protocol_id;

    /* Memory */
    void*  allocator_context;
    void*  (*allocate_function)(void* ctx, size_t bytes);
    void   (*free_function)(void* ctx, void* ptr);

    /* Channels (must match server) */
    netudp_channel_config_t channels[255];
    int num_channels;

    /* Compression (optional) */
    const void* compression_dict;
    uint8_t     compression_level;

    /* Logging */
    int  log_level;
    void (*log_callback)(int level, const char* msg);
} netudp_client_config_t;

#ifdef __cplusplus
}
#endif

#endif /* NETUDP_TYPES_H */
