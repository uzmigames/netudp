#ifndef NETUDP_GROUP_H
#define NETUDP_GROUP_H

/**
 * @file group.h
 * @brief Multicast groups — send to a subset of connected clients.
 *
 * Each group maintains a compact array of member slot indices with O(1)
 * add/remove (swap-remove pattern, same as active_slots). Clients can
 * belong to multiple groups simultaneously (zone + party + raid + guild).
 *
 * Phase 40: foundation for MMORPG replication (phases 41-44).
 */

#include <cstdint>
#include <cstring>

namespace netudp {

static constexpr int kMaxGroupMembers = 8192; /* Max members per group */

struct Group {
    bool  active = false;
    int   group_id = -1;

    /* Compact member list — same swap-remove pattern as server active_slots */
    int*  members = nullptr;       /* Array of client slot indices */
    int   member_count = 0;
    int   member_capacity = 0;

    /* Reverse map: client_slot → position in members[] (-1 if not member) */
    int*  slot_to_pos = nullptr;   /* Indexed by client slot, size = max_clients */

    /** Add a client slot to this group. Returns true on success. */
    bool add(int slot) {
        if (slot < 0 || slot_to_pos == nullptr) return false;
        if (slot_to_pos[slot] >= 0) return false; /* Already a member */
        if (member_count >= member_capacity) return false; /* Full */

        slot_to_pos[slot] = member_count;
        members[member_count] = slot;
        member_count++;
        return true;
    }

    /** Remove a client slot from this group. Returns true on success. */
    bool remove(int slot) {
        if (slot < 0 || slot_to_pos == nullptr) return false;
        int pos = slot_to_pos[slot];
        if (pos < 0) return false; /* Not a member */

        /* Swap-remove: move last member into this position */
        int last = --member_count;
        if (pos < last) {
            int moved_slot = members[last];
            members[pos] = moved_slot;
            slot_to_pos[moved_slot] = pos;
        }
        slot_to_pos[slot] = -1;
        return true;
    }

    /** Check if a client slot is a member. */
    bool has(int slot) const {
        if (slot < 0 || slot_to_pos == nullptr) return false;
        return slot_to_pos[slot] >= 0;
    }

    /** Initialize storage. Called once from group_create. */
    void init(int id, int max_clients, int* member_buf, int* slot_buf, int capacity) {
        active = true;
        group_id = id;
        members = member_buf;
        slot_to_pos = slot_buf;
        member_count = 0;
        member_capacity = capacity;
        std::memset(slot_to_pos, 0xFF, static_cast<size_t>(max_clients) * sizeof(int)); /* -1 */
    }

    /** Clear all members without freeing storage. */
    void clear() {
        if (slot_to_pos != nullptr && member_count > 0) {
            for (int i = 0; i < member_count; ++i) {
                slot_to_pos[members[i]] = -1;
            }
        }
        member_count = 0;
    }

    /** Reset to inactive state. */
    void reset() {
        clear();
        active = false;
        group_id = -1;
        members = nullptr;
        slot_to_pos = nullptr;
        member_count = 0;
        member_capacity = 0;
    }
};

} // namespace netudp

#endif /* NETUDP_GROUP_H */
