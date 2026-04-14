#ifndef NETUDP_REPLICATE_H
#define NETUDP_REPLICATE_H

/**
 * @file replicate.h
 * @brief Dirty-property serialization, deserialization, and server replicate loop.
 *
 * Wire format per entity:
 *   [entity_id: u16][dirty_mask: varint (1-8 bytes)][prop_values...]
 *
 * Only dirty properties are serialized. Quantized types (VEC3, QUAT, F32 with
 * REP_QUANTIZE) use compact encodings.
 *
 * Phase 42.
 */

#include "schema.h"
#include "entity.h"
#include <cstdint>

namespace netudp {

/**
 * Serialize dirty properties of an entity into wire buffer.
 * @param ent       Entity to serialize
 * @param out       Output buffer (must have capacity >= kEntityMaxValueSize + 16)
 * @param out_cap   Capacity of output buffer
 * @param filter_owner  Owner client index for REP_OWNER_ONLY/REP_SKIP_OWNER filtering (-1 = no filter)
 * @param target_client Target client index being serialized for
 * @param is_initial    True if this is the initial snapshot (includes REP_INITIAL_ONLY props)
 * @return Number of bytes written, or -1 on error
 */
int replicate_serialize(const Entity& ent, uint8_t* out, int out_cap,
                        int target_client, bool is_initial);

/**
 * Deserialize wire buffer into entity property values.
 * @param schema    Schema for decoding property types
 * @param wire      Input wire buffer
 * @param wire_len  Length of wire data
 * @param out_eid   Output: entity_id read from wire
 * @param out_values Output: property values buffer to fill
 * @param out_dirty  Output: dirty mask of which properties were in the packet
 * @return Number of bytes consumed, or -1 on error
 */
int replicate_deserialize(const Schema& schema, const uint8_t* wire, int wire_len,
                          uint16_t* out_eid, uint8_t* out_values, uint64_t* out_dirty);

/** Write a varint (1-8 bytes). Returns bytes written. */
int varint_encode(uint64_t val, uint8_t* out);

/** Read a varint. Returns bytes consumed, or -1 on error. */
int varint_decode(const uint8_t* data, int len, uint64_t* out);

} // namespace netudp

#endif /* NETUDP_REPLICATE_H */
