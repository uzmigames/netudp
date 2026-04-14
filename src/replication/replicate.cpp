#include "replicate.h"
#include "schema.h"
#include "entity.h"

#include <cstring>
#include <cmath>
#include <algorithm>

namespace netudp {

/* ======================================================================
 * Varint encoding (LEB128-style, 7 bits per byte, MSB = continuation)
 * ====================================================================== */

int varint_encode(uint64_t val, uint8_t* out) {
    int pos = 0;
    do {
        uint8_t byte = static_cast<uint8_t>(val & 0x7F);
        val >>= 7;
        if (val != 0) { byte |= 0x80; }
        out[pos++] = byte;
    } while (val != 0);
    return pos;
}

int varint_decode(const uint8_t* data, int len, uint64_t* out) {
    uint64_t result = 0;
    int shift = 0;
    int pos = 0;
    while (pos < len && shift < 64) {
        uint8_t byte = data[pos];
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;
        pos++;
        if ((byte & 0x80) == 0) {
            *out = result;
            return pos;
        }
        shift += 7;
    }
    return -1; /* Truncated */
}

/* ======================================================================
 * Quantization
 * ====================================================================== */

uint16_t f32_to_half(float v) {
    /* Simple conversion — handles normal range, flush denorms to zero */
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    uint32_t sign = (bits >> 16) & 0x8000;
    int32_t  exp  = static_cast<int32_t>((bits >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = (bits >> 13) & 0x3FF;

    if (exp <= 0) { return static_cast<uint16_t>(sign); }        /* Flush to zero */
    if (exp >= 31) { return static_cast<uint16_t>(sign | 0x7C00); } /* Inf */
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | mant);
}

float half_to_f32(uint16_t h) {
    uint32_t sign = static_cast<uint32_t>(h & 0x8000) << 16;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;

    if (exp == 0) {
        /* Zero or denorm → flush to zero */
        uint32_t bits = sign;
        float result;
        std::memcpy(&result, &bits, 4);
        return result;
    }
    if (exp == 31) {
        uint32_t bits = sign | 0x7F800000 | (mant << 13);
        float result;
        std::memcpy(&result, &bits, 4);
        return result;
    }
    uint32_t bits = sign | ((exp - 15 + 127) << 23) | (mant << 13);
    float result;
    std::memcpy(&result, &bits, 4);
    return result;
}

static inline int32_t clamp_i(int32_t v, int32_t lo, int32_t hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

uint32_t vec3_quantize(const float v[3]) {
    /* 11+11+10 bit packing. X,Y: 11 bits signed (-1024..1023, 0.5 precision).
     * Z: 10 bits signed (-512..511, 0.5 precision). Total = 32 bits. */
    int32_t x = clamp_i(static_cast<int32_t>(v[0] * 2.0f), -1024, 1023);
    int32_t y = clamp_i(static_cast<int32_t>(v[1] * 2.0f), -1024, 1023);
    int32_t z = clamp_i(static_cast<int32_t>(v[2] * 2.0f), -512, 511);
    uint32_t ux = static_cast<uint32_t>(x + 1024) & 0x7FF; /* 11 bits */
    uint32_t uy = static_cast<uint32_t>(y + 1024) & 0x7FF; /* 11 bits */
    uint32_t uz = static_cast<uint32_t>(z + 512) & 0x3FF;  /* 10 bits */
    return (ux) | (uy << 11) | (uz << 22);
}

void vec3_dequantize(uint32_t packed, float out[3]) {
    int32_t x = static_cast<int32_t>(packed & 0x7FF) - 1024;
    int32_t y = static_cast<int32_t>((packed >> 11) & 0x7FF) - 1024;
    int32_t z = static_cast<int32_t>((packed >> 22) & 0x3FF) - 512;
    out[0] = static_cast<float>(x) * 0.5f;
    out[1] = static_cast<float>(y) * 0.5f;
    out[2] = static_cast<float>(z) * 0.5f;
}

uint32_t quat_quantize(const float q[4]) {
    /* Smallest-three: drop the component with largest absolute value,
     * encode 2-bit index + 3x10-bit signed values (-1..1 → 0..1023). */
    int max_idx = 0;
    float max_abs = std::fabs(q[0]);
    for (int i = 1; i < 4; ++i) {
        float a = std::fabs(q[i]);
        if (a > max_abs) { max_abs = a; max_idx = i; }
    }

    /* Ensure the dropped component is positive (negate quat if needed) */
    float sign = (q[max_idx] < 0.0f) ? -1.0f : 1.0f;

    float small[3];
    int si = 0;
    for (int i = 0; i < 4; ++i) {
        if (i != max_idx) {
            small[si++] = q[i] * sign; /* Normalize to [-0.707..0.707] */
        }
    }

    /* Map [-1..1] → [0..1023] (10-bit) */
    auto to10 = [](float v) -> uint32_t {
        int32_t mapped = static_cast<int32_t>((v * 0.5f + 0.5f) * 1023.0f + 0.5f);
        return static_cast<uint32_t>(clamp_i(mapped, 0, 1023));
    };

    return static_cast<uint32_t>(max_idx) << 30 |
           to10(small[0]) << 20 |
           to10(small[1]) << 10 |
           to10(small[2]);
}

void quat_dequantize(uint32_t packed, float out[4]) {
    int max_idx = static_cast<int>((packed >> 30) & 0x3);

    auto from10 = [](uint32_t v) -> float {
        return (static_cast<float>(v) / 1023.0f - 0.5f) * 2.0f;
    };

    float small[3];
    small[0] = from10((packed >> 20) & 0x3FF);
    small[1] = from10((packed >> 10) & 0x3FF);
    small[2] = from10(packed & 0x3FF);

    /* Reconstruct the dropped component: w = sqrt(1 - x^2 - y^2 - z^2) */
    float sum_sq = small[0] * small[0] + small[1] * small[1] + small[2] * small[2];
    float w = (sum_sq < 1.0f) ? std::sqrt(1.0f - sum_sq) : 0.0f;

    int si = 0;
    for (int i = 0; i < 4; ++i) {
        if (i == max_idx) {
            out[i] = w;
        } else {
            out[i] = small[si++];
        }
    }
}

/* ======================================================================
 * Serialize: entity dirty props → wire buffer
 * ====================================================================== */

static int write_prop(const PropDesc& pd, const uint8_t* val, uint8_t* out, int cap) {
    if (pd.rep_flags & REP_QUANTIZE) {
        switch (pd.type) {
            case PropType::VEC3: {
                if (cap < 4) { return -1; }
                float v[3];
                std::memcpy(v, val, 12);
                uint32_t packed = vec3_quantize(v);
                std::memcpy(out, &packed, 4);
                return 4;
            }
            case PropType::QUAT: {
                if (cap < 4) { return -1; }
                float q[4];
                std::memcpy(q, val, 16);
                uint32_t packed = quat_quantize(q);
                std::memcpy(out, &packed, 4);
                return 4;
            }
            case PropType::F32: {
                if (cap < 2) { return -1; }
                float v;
                std::memcpy(&v, val, 4);
                uint16_t h = f32_to_half(v);
                std::memcpy(out, &h, 2);
                return 2;
            }
            default:
                break;
        }
    }

    /* Raw encoding */
    int sz = pd.raw_size;
    if (cap < sz) { return -1; }
    std::memcpy(out, val, static_cast<size_t>(sz));
    return sz;
}

int replicate_serialize(const Entity& ent, uint8_t* out, int out_cap,
                        int target_client, bool is_initial) {
    if (ent.schema == nullptr || out == nullptr) { return -1; }

    const Schema& s = *ent.schema;
    int pos = 0;

    /* Entity ID (u16) */
    if (pos + 2 > out_cap) { return -1; }
    std::memcpy(out + pos, &ent.entity_id, 2);
    pos += 2;

    /* Build filtered dirty mask */
    uint64_t mask = is_initial ? ((1ULL << s.prop_count) - 1) : ent.dirty_mask;

    /* Filter by replication conditions */
    for (int i = 0; i < s.prop_count; ++i) {
        uint64_t bit = 1ULL << i;
        if ((mask & bit) == 0) { continue; }

        uint16_t flags = s.props[i].rep_flags;

        /* INITIAL_ONLY: only include in initial snapshot */
        if ((flags & REP_INITIAL_ONLY) != 0 && !is_initial) {
            mask &= ~bit;
            continue;
        }

        /* OWNER_ONLY: only to owner */
        if ((flags & REP_OWNER_ONLY) != 0 && target_client != ent.owner_client) {
            mask &= ~bit;
            continue;
        }

        /* SKIP_OWNER: everyone except owner */
        if ((flags & REP_SKIP_OWNER) != 0 && target_client == ent.owner_client) {
            mask &= ~bit;
            continue;
        }
    }

    if (mask == 0) { return 0; } /* Nothing to send for this client */

    /* Dirty mask (varint) */
    int vi_len = varint_encode(mask, out + pos);
    pos += vi_len;

    /* Property values (only dirty ones) */
    for (int i = 0; i < s.prop_count; ++i) {
        if ((mask & (1ULL << i)) == 0) { continue; }
        const uint8_t* val = ent.values + s.props[i].offset;
        int written = write_prop(s.props[i], val, out + pos, out_cap - pos);
        if (written < 0) { return -1; }
        pos += written;
    }

    return pos;
}

/* ======================================================================
 * Deserialize: wire buffer → property values
 * ====================================================================== */

static int read_prop(const PropDesc& pd, const uint8_t* wire, int cap,
                     uint8_t* out_val) {
    if (pd.rep_flags & REP_QUANTIZE) {
        switch (pd.type) {
            case PropType::VEC3: {
                if (cap < 4) { return -1; }
                uint32_t packed;
                std::memcpy(&packed, wire, 4);
                float v[3];
                vec3_dequantize(packed, v);
                std::memcpy(out_val, v, 12);
                return 4;
            }
            case PropType::QUAT: {
                if (cap < 4) { return -1; }
                uint32_t packed;
                std::memcpy(&packed, wire, 4);
                float q[4];
                quat_dequantize(packed, q);
                std::memcpy(out_val, q, 16);
                return 4;
            }
            case PropType::F32: {
                if (cap < 2) { return -1; }
                uint16_t h;
                std::memcpy(&h, wire, 2);
                float v = half_to_f32(h);
                std::memcpy(out_val, &v, 4);
                return 2;
            }
            default:
                break;
        }
    }

    int sz = pd.raw_size;
    if (cap < sz) { return -1; }
    std::memcpy(out_val, wire, static_cast<size_t>(sz));
    return sz;
}

int replicate_deserialize(const Schema& schema, const uint8_t* wire, int wire_len,
                          uint16_t* out_eid, uint8_t* out_values, uint64_t* out_dirty) {
    if (wire_len < 3) { return -1; } /* entity_id(2) + at least 1 byte varint */

    int pos = 0;

    /* Entity ID */
    std::memcpy(out_eid, wire + pos, 2);
    pos += 2;

    /* Dirty mask */
    uint64_t mask = 0;
    int vi_len = varint_decode(wire + pos, wire_len - pos, &mask);
    if (vi_len < 0) { return -1; }
    pos += vi_len;

    *out_dirty = mask;

    /* Property values */
    for (int i = 0; i < schema.prop_count; ++i) {
        if ((mask & (1ULL << i)) == 0) { continue; }
        uint8_t* dest = out_values + schema.props[i].offset;
        int consumed = read_prop(schema.props[i], wire + pos, wire_len - pos, dest);
        if (consumed < 0) { return -1; }
        pos += consumed;
    }

    return pos;
}

} // namespace netudp
