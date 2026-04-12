#ifndef NETUDP_COMPRESSOR_H
#define NETUDP_COMPRESSOR_H

/**
 * @file compressor.h
 * @brief Per-channel compression wrapper for netc (spec 11).
 *
 * Stateful mode (reliable ordered): context preserved across messages.
 * Stateless mode (unreliable): fresh context per message.
 * Passthrough guarantee: never expands payload.
 *
 * When NETUDP_HAS_NETC is defined, uses the real netc library.
 * Otherwise provides a passthrough implementation (no compression).
 */

#include <cstdint>
#include <cstring>

#if defined(NETUDP_HAS_NETC)
#include <netc.h>
#endif

namespace netudp {

/** Compression mode per channel. */
enum class CompressionMode : uint8_t {
    None      = 0,
    Stateful  = 1, /* Reliable ordered: context preserved */
    Stateless = 2, /* Unreliable: fresh per message */
};

/**
 * Per-channel compressor instance.
 * Wraps netc context for stateful/stateless compression.
 */
class Compressor {
public:
    /**
     * Initialize compressor for a channel.
     * @param mode       Compression mode
     * @param dict_ptr   Opaque pointer to netc_dict_t (or nullptr for no compression)
     */
    bool init(CompressionMode mode, const void* dict_ptr) {
        mode_ = mode;
        dict_ = dict_ptr;

        if (mode == CompressionMode::None || dict_ptr == nullptr) {
            mode_ = CompressionMode::None;
            return true;
        }

#if defined(NETUDP_HAS_NETC)
        netc_cfg_t cfg = {};
        if (mode == CompressionMode::Stateful) {
            cfg.flags = NETC_CFG_FLAG_STATEFUL | NETC_CFG_FLAG_DELTA | NETC_CFG_FLAG_COMPACT_HDR;
        } else {
            cfg.flags = NETC_CFG_FLAG_STATELESS | NETC_CFG_FLAG_COMPACT_HDR;
        }

        ctx_ = netc_ctx_create(static_cast<const netc_dict_t*>(dict_ptr), &cfg);
        if (ctx_ == nullptr) {
            mode_ = CompressionMode::None;
            return false;
        }
#endif
        return true;
    }

    void destroy() {
#if defined(NETUDP_HAS_NETC)
        if (ctx_ != nullptr) {
            netc_ctx_destroy(ctx_);
            ctx_ = nullptr;
        }
#endif
        mode_ = CompressionMode::None;
    }

    ~Compressor() { destroy(); }

    /**
     * Compress data. Passthrough guarantee: if compressed >= original, returns original.
     * @param src       Input data
     * @param src_len   Input length
     * @param dst       Output buffer (must have capacity src_len + 16 for header)
     * @param dst_cap   Output buffer capacity
     * @param compressed  Set to true if compression was applied, false if passthrough
     * @return          Output length (compressed or original)
     */
    int compress(const uint8_t* src, int src_len,
                 uint8_t* dst, int dst_cap, bool* compressed) {
        *compressed = false;

        if (mode_ == CompressionMode::None || src_len <= 0) {
            std::memcpy(dst, src, static_cast<size_t>(src_len));
            return src_len;
        }

#if defined(NETUDP_HAS_NETC)
        size_t dst_size = 0;
        netc_result_t r;

        if (mode_ == CompressionMode::Stateful) {
            r = netc_compress(ctx_, src, static_cast<size_t>(src_len),
                              dst, static_cast<size_t>(dst_cap), &dst_size);
        } else {
            r = netc_compress_stateless(
                static_cast<const netc_dict_t*>(dict_), src, static_cast<size_t>(src_len),
                dst, static_cast<size_t>(dst_cap), &dst_size);
        }

        if (r == NETC_OK && static_cast<int>(dst_size) < src_len) {
            *compressed = true;
            return static_cast<int>(dst_size);
        }
#endif
        /* Passthrough: compressed >= original or no netc */
        std::memcpy(dst, src, static_cast<size_t>(src_len));
        return src_len;
    }

    /**
     * Decompress data.
     * @return Decompressed length, or -1 on error.
     */
    int decompress(const uint8_t* src, int src_len,
                   uint8_t* dst, int dst_cap) {
        if (mode_ == CompressionMode::None || src_len <= 0) {
            std::memcpy(dst, src, static_cast<size_t>(src_len));
            return src_len;
        }

#if defined(NETUDP_HAS_NETC)
        size_t dst_size = 0;
        netc_result_t r;

        if (mode_ == CompressionMode::Stateful) {
            r = netc_decompress(ctx_, src, static_cast<size_t>(src_len),
                                dst, static_cast<size_t>(dst_cap), &dst_size);
        } else {
            r = netc_decompress_stateless(
                static_cast<const netc_dict_t*>(dict_), src, static_cast<size_t>(src_len),
                dst, static_cast<size_t>(dst_cap), &dst_size);
        }

        if (r == NETC_OK) {
            return static_cast<int>(dst_size);
        }
        return -1;
#else
        std::memcpy(dst, src, static_cast<size_t>(src_len));
        return src_len;
#endif
    }

    CompressionMode mode() const { return mode_; }
    bool enabled() const { return mode_ != CompressionMode::None; }

private:
    CompressionMode mode_ = CompressionMode::None;
    const void* dict_ = nullptr;
#if defined(NETUDP_HAS_NETC)
    netc_ctx_t* ctx_ = nullptr;
#endif
};

} // namespace netudp

#endif /* NETUDP_COMPRESSOR_H */
