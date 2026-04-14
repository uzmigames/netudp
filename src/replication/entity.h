#ifndef NETUDP_ENTITY_H
#define NETUDP_ENTITY_H

/**
 * @file entity.h
 * @brief Replicated entity — instance of a Schema with dirty-tracked property values.
 *
 * Each entity has a fixed-size value buffer, a 64-bit dirty mask, an owner
 * client index, and a group binding. Typed setters compare old vs new value
 * and only set the dirty bit on actual change.
 *
 * Phase 42.
 */

#include "schema.h"
#include <cstdint>
#include <cstring>
#include <cmath>

namespace netudp {

static constexpr int kEntityMaxValueSize = 2048; /* Max bytes for all property values */
static constexpr uint16_t kEntityIdNone = 0;

struct Entity {
    bool     active = false;
    uint16_t entity_id = kEntityIdNone;
    int      schema_id = -1;
    const Schema* schema = nullptr;

    int      owner_client = -1;  /* Client index that "owns" this entity (-1 = no owner) */
    int      group_id = -1;      /* Multicast group for replication (-1 = no group) */

    uint64_t dirty_mask = 0;     /* Bit i = property i has changed since last replicate */
    uint64_t initial_mask = 0;   /* Tracks which clients have received initial snapshot (per-client, simplified) */
    bool     needs_initial = true; /* True until first full replicate */

    /* Priority + rate limiting (phase 43) */
    uint8_t  priority = 128;         /* 0-255: higher = more important. Default mid-range. */
    float    max_rate_hz = 20.0f;    /* Max replication frequency. 0 = unlimited. */
    double   last_replicate_time = 0.0;  /* Timestamp of last successful replicate */
    float    min_update_interval = 2.0f; /* Starvation prevention: guaranteed update interval */

    uint8_t  values[kEntityMaxValueSize] = {};

    /* ---- Typed setters (set dirty bit on change) ---- */

    bool set_u8(int prop_idx, uint8_t val) {
        if (!valid_prop(prop_idx, PropType::U8)) { return false; }
        uint8_t* p = values + schema->props[prop_idx].offset;
        if (*p == val) { return false; }
        *p = val;
        dirty_mask |= (1ULL << prop_idx);
        return true;
    }

    bool set_u16(int prop_idx, uint16_t val) {
        if (!valid_prop(prop_idx, PropType::U16)) { return false; }
        uint16_t old;
        std::memcpy(&old, values + schema->props[prop_idx].offset, 2);
        if (old == val) { return false; }
        std::memcpy(values + schema->props[prop_idx].offset, &val, 2);
        dirty_mask |= (1ULL << prop_idx);
        return true;
    }

    bool set_i32(int prop_idx, int32_t val) {
        if (!valid_prop(prop_idx, PropType::I32)) { return false; }
        int32_t old;
        std::memcpy(&old, values + schema->props[prop_idx].offset, 4);
        if (old == val) { return false; }
        std::memcpy(values + schema->props[prop_idx].offset, &val, 4);
        dirty_mask |= (1ULL << prop_idx);
        return true;
    }

    bool set_f32(int prop_idx, float val) {
        if (!valid_prop(prop_idx, PropType::F32)) { return false; }
        float old;
        std::memcpy(&old, values + schema->props[prop_idx].offset, 4);
        if (old == val) { return false; }
        std::memcpy(values + schema->props[prop_idx].offset, &val, 4);
        dirty_mask |= (1ULL << prop_idx);
        return true;
    }

    bool set_vec3(int prop_idx, const float v[3]) {
        if (!valid_prop(prop_idx, PropType::VEC3)) { return false; }
        uint8_t* p = values + schema->props[prop_idx].offset;
        if (std::memcmp(p, v, 12) == 0) { return false; }
        std::memcpy(p, v, 12);
        dirty_mask |= (1ULL << prop_idx);
        return true;
    }

    bool set_quat(int prop_idx, const float q[4]) {
        if (!valid_prop(prop_idx, PropType::QUAT)) { return false; }
        uint8_t* p = values + schema->props[prop_idx].offset;
        if (std::memcmp(p, q, 16) == 0) { return false; }
        std::memcpy(p, q, 16);
        dirty_mask |= (1ULL << prop_idx);
        return true;
    }

    bool set_blob(int prop_idx, const void* data, int len) {
        if (!valid_prop(prop_idx, PropType::BLOB)) { return false; }
        const PropDesc& pd = schema->props[prop_idx];
        if (len > pd.blob_capacity) { return false; }
        uint8_t* p = values + pd.offset;
        if (std::memcmp(p, data, static_cast<size_t>(len)) == 0) { return false; }
        std::memcpy(p, data, static_cast<size_t>(len));
        dirty_mask |= (1ULL << prop_idx);
        return true;
    }

    /* ---- Typed getters ---- */

    uint8_t  get_u8(int prop_idx)  const { return valid_prop(prop_idx, PropType::U8) ? values[schema->props[prop_idx].offset] : 0; }
    uint16_t get_u16(int prop_idx) const { uint16_t v = 0; if (valid_prop(prop_idx, PropType::U16)) { std::memcpy(&v, values + schema->props[prop_idx].offset, 2); } return v; }
    int32_t  get_i32(int prop_idx) const { int32_t v = 0; if (valid_prop(prop_idx, PropType::I32)) { std::memcpy(&v, values + schema->props[prop_idx].offset, 4); } return v; }
    float    get_f32(int prop_idx) const { float v = 0; if (valid_prop(prop_idx, PropType::F32)) { std::memcpy(&v, values + schema->props[prop_idx].offset, 4); } return v; }

    void get_vec3(int prop_idx, float out[3]) const {
        if (valid_prop(prop_idx, PropType::VEC3)) {
            std::memcpy(out, values + schema->props[prop_idx].offset, 12);
        } else {
            out[0] = out[1] = out[2] = 0.0f;
        }
    }

    void get_quat(int prop_idx, float out[4]) const {
        if (valid_prop(prop_idx, PropType::QUAT)) {
            std::memcpy(out, values + schema->props[prop_idx].offset, 16);
        } else {
            out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f;
        }
    }

    /* ---- Lifecycle ---- */

    void reset() {
        active = false;
        entity_id = kEntityIdNone;
        schema_id = -1;
        schema = nullptr;
        owner_client = -1;
        group_id = -1;
        dirty_mask = 0;
        initial_mask = 0;
        needs_initial = true;
        priority = 128;
        max_rate_hz = 20.0f;
        last_replicate_time = 0.0;
        min_update_interval = 2.0f;
        std::memset(values, 0, sizeof(values));
    }

private:
    bool valid_prop(int idx, PropType expected) const {
        if (schema == nullptr || idx < 0 || idx >= schema->prop_count) { return false; }
        return schema->props[idx].type == expected;
    }
};

} // namespace netudp

#endif /* NETUDP_ENTITY_H */
