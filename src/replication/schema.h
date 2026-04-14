#ifndef NETUDP_SCHEMA_H
#define NETUDP_SCHEMA_H

/**
 * @file schema.h
 * @brief Property replication schema — Unreal-style UPROPERTY(Replicated).
 *
 * A schema defines the property layout for an entity type: each property has
 * a name, type, byte size, offset in the value buffer, replication condition,
 * and bit index in the 64-bit dirty mask (max 64 properties per schema).
 *
 * Phase 42: foundation for MMORPG entity replication.
 */

#include <cstdint>
#include <cstring>

namespace netudp {

/* ---- Property types ---- */

enum class PropType : uint8_t {
    U8    = 0,
    U16   = 1,
    I32   = 2,
    F32   = 3,
    VEC3  = 4,   /* 3x float = 12 bytes raw, 4 bytes quantized (10+11+10) */
    QUAT  = 5,   /* 4x float = 16 bytes raw, 4 bytes quantized (smallest-three) */
    BLOB  = 6,   /* Fixed-size byte array */
};

/* ---- Replication condition flags (combinable) ---- */

static constexpr uint16_t REP_ALL          = 0x0000; /* Replicate to all observers */
static constexpr uint16_t REP_OWNER_ONLY   = 0x0001; /* Only to the owning client */
static constexpr uint16_t REP_SKIP_OWNER   = 0x0002; /* Everyone except the owner */
static constexpr uint16_t REP_INITIAL_ONLY = 0x0004; /* Only on spawn / first observe */
static constexpr uint16_t REP_NOTIFY       = 0x0008; /* Client fires callback on change */
static constexpr uint16_t REP_UNRELIABLE   = 0x0000; /* Send via unreliable channel (default) */
static constexpr uint16_t REP_RELIABLE     = 0x0010; /* Send via reliable channel */
static constexpr uint16_t REP_QUANTIZE     = 0x0020; /* Use quantized encoding (vec3/quat/f32) */

/* ---- Property descriptor ---- */

static constexpr int kMaxPropName = 32;
static constexpr int kMaxProperties = 64;

struct PropDesc {
    char     name[kMaxPropName] = {};
    PropType type = PropType::U8;
    uint16_t rep_flags = REP_ALL;
    int      raw_size = 0;       /* Byte size of the raw value (e.g., 12 for vec3) */
    int      wire_size = 0;      /* Byte size on wire (may differ if quantized) */
    int      offset = 0;         /* Byte offset into entity value buffer */
    int      bit_index = 0;      /* Bit position in dirty mask (0-63) */
    int      blob_capacity = 0;  /* For BLOB type: max bytes */
};

/* ---- Schema ---- */

struct Schema {
    int      schema_id = -1;
    int      prop_count = 0;
    int      value_buffer_size = 0;  /* Total bytes needed for all property values */
    PropDesc props[kMaxProperties];

    int add_prop(const char* name, PropType type, uint16_t flags, int raw_size, int wire_size, int blob_cap = 0) {
        if (prop_count >= kMaxProperties) { return -1; }
        int idx = prop_count;
        PropDesc& p = props[idx];
        std::strncpy(p.name, name, kMaxPropName - 1);
        p.name[kMaxPropName - 1] = '\0';
        p.type = type;
        p.rep_flags = flags;
        p.raw_size = raw_size;
        p.wire_size = (flags & REP_QUANTIZE) ? wire_size : raw_size;
        p.offset = value_buffer_size;
        p.bit_index = idx;
        p.blob_capacity = blob_cap;
        value_buffer_size += raw_size;
        prop_count++;
        return idx;
    }

    int find_prop(const char* name) const {
        for (int i = 0; i < prop_count; ++i) {
            if (std::strncmp(props[i].name, name, kMaxPropName) == 0) {
                return i;
            }
        }
        return -1;
    }
};

/* ---- Quantization helpers ---- */

/** Quantize float to 16-bit half-float (IEEE 754 binary16). */
uint16_t f32_to_half(float v);
float    half_to_f32(uint16_t h);

/** Quantize vec3 to 32 bits (10+11+10 format, range -1024..1024). */
uint32_t vec3_quantize(const float v[3]);
void     vec3_dequantize(uint32_t packed, float out[3]);

/** Quantize unit quaternion to 32 bits (smallest-three, 2-bit index + 3x10-bit). */
uint32_t quat_quantize(const float q[4]);
void     quat_dequantize(uint32_t packed, float out[4]);

} // namespace netudp

#endif /* NETUDP_SCHEMA_H */
