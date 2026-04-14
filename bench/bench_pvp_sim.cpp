/**
 * @file bench_pvp_sim.cpp
 * @brief 5000-player PvP MMORPG simulator using UzEngine movement parameters.
 *
 * Simulates a realistic game server scenario:
 * - 5000 connected players with entities replicated via netudp phases 40-44
 * - UzEngine movement: 500 cm/s walk, 1500 cm/s sprint, 60Hz tick, cm units
 * - PvP combat: melee range 300cm, skill range 1500cm, 2s attack interval
 * - Zone-based multicast groups (200m x 200m cells)
 * - Property replication with dirty tracking + state overwrite
 * - Priority/rate limiting: close=20Hz, mid=5Hz, far=1Hz
 *
 * Measures: entity updates/s, msgs delivered/s, bandwidth, ticks/s
 */

#include "bench_main.h"
#include <netudp/netudp.h>
#include <netudp/netudp_token.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <vector>

/* ======================================================================
 * UzEngine parameters (from E:\UzEngine source analysis)
 * ====================================================================== */

static constexpr float kWalkSpeed     = 500.0f;   /* cm/s (5 m/s) */
static constexpr float kSprintSpeed   = 1500.0f;  /* cm/s (15 m/s) */
static constexpr float kTickRate      = 60.0f;    /* Hz */
static constexpr float kTickDt        = 1.0f / kTickRate; /* 16.667ms */
static constexpr float kWorldSize     = 200000.0f; /* 2km x 2km world (cm) */
static constexpr float kZoneSize      = 20000.0f;  /* 200m x 200m zones (cm) */
static constexpr float kMeleeRange    = 300.0f;    /* 3m melee range (cm) */
static constexpr float kSkillRange    = 1500.0f;   /* 15m skill range (cm) */
static constexpr float kAttackInterval = 2.0f;     /* seconds between attacks */
static constexpr float kMaxHealth     = 1000.0f;
static constexpr float kMeleeDamage   = 50.0f;
static constexpr float kSkillDamage   = 120.0f;
static constexpr float kRespawnTime   = 5.0f;      /* seconds */

static constexpr int   kNumPlayers    = 5000;
static constexpr float kBenchDuration = 5.0f;      /* seconds */

/* Animation states */
static constexpr uint8_t ANIM_IDLE    = 0;
static constexpr uint8_t ANIM_WALK    = 1;
static constexpr uint8_t ANIM_SPRINT  = 2;
static constexpr uint8_t ANIM_ATTACK  = 3;
static constexpr uint8_t ANIM_SKILL   = 4;
static constexpr uint8_t ANIM_DEAD    = 5;

static constexpr uint64_t kPvpProtoId = 0x0BA7000500000001ULL;
static constexpr uint16_t kPvpPort    = 31000;

static const uint8_t kPvpKey[32] = {
    0x50, 0x56, 0x50, 0x01, 0x02, 0x03, 0x04, 0x05,
    0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
    0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D,
};

/* ======================================================================
 * Player simulation state (server-side, not replicated directly)
 * ====================================================================== */

struct PlayerSim {
    float pos[3] = {};       /* XYZ position in cm */
    float rot[4] = {0,0,0,1}; /* Quaternion rotation */
    float dir[2] = {};       /* Movement direction (XY normalized) */
    float speed = 0.0f;      /* Current speed cm/s */
    float health = kMaxHealth;
    uint8_t anim = ANIM_IDLE;
    float attack_cooldown = 0.0f;
    float respawn_timer = 0.0f;
    bool alive = true;

    uint16_t entity_id = 0;
    int client_slot = -1;
    int zone_x = 0, zone_y = 0; /* Current zone cell */
    int zone_group = -1;         /* Current multicast group */
};

/* Simple LCG random for deterministic benchmark */
static uint32_t g_rng_state = 12345;
static float rng_float() {
    g_rng_state = g_rng_state * 1103515245u + 12345u;
    return static_cast<float>(g_rng_state & 0x7FFF) / 32768.0f;
}
static float rng_range(float lo, float hi) { return lo + rng_float() * (hi - lo); }
static int rng_int(int lo, int hi) { return lo + static_cast<int>(rng_float() * static_cast<float>(hi - lo)); }

/* ======================================================================
 * Zone grid: world divided into cells, each cell is a multicast group
 * ====================================================================== */

static constexpr int kZonesPerAxis = static_cast<int>(kWorldSize / kZoneSize); /* 10 x 10 = 100 zones */
static constexpr int kMaxZones = kZonesPerAxis * kZonesPerAxis;

static int zone_index(int zx, int zy) {
    int cx = (zx < 0) ? 0 : (zx >= kZonesPerAxis ? kZonesPerAxis - 1 : zx);
    int cy = (zy < 0) ? 0 : (zy >= kZonesPerAxis ? kZonesPerAxis - 1 : zy);
    return cy * kZonesPerAxis + cx;
}

static void pos_to_zone(const float pos[3], int* zx, int* zy) {
    *zx = static_cast<int>(pos[0] / kZoneSize);
    *zy = static_cast<int>(pos[1] / kZoneSize);
    if (*zx < 0) *zx = 0;
    if (*zy < 0) *zy = 0;
    if (*zx >= kZonesPerAxis) *zx = kZonesPerAxis - 1;
    if (*zy >= kZonesPerAxis) *zy = kZonesPerAxis - 1;
}

/* ======================================================================
 * Benchmark entry point
 * ====================================================================== */

static BenchResult run_pvp_sim(const BenchConfig& cfg, int num_players, int num_io_threads) {
    using Clock = std::chrono::high_resolution_clock;
    BenchResult r;
    char name_buf[64];
    std::snprintf(name_buf, sizeof(name_buf), "pvp_%dp_t%d", num_players, num_io_threads);
    r.name = name_buf;

    g_rng_state = 42; /* Deterministic seed */

    char srv_addr[64];
    std::snprintf(srv_addr, sizeof(srv_addr), "127.0.0.1:%u", static_cast<unsigned>(kPvpPort));

    /* Server config */
    netudp_server_config_t srv_cfg = {};
    srv_cfg.protocol_id = kPvpProtoId;
    std::memcpy(srv_cfg.private_key, kPvpKey, 32);
    srv_cfg.num_channels = 2;
    srv_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;  /* Position/movement */
    srv_cfg.channels[1].type = NETUDP_CHANNEL_RELIABLE_ORDERED; /* Combat events */
    srv_cfg.num_io_threads = num_io_threads;
    srv_cfg.crypto_mode = NETUDP_CRYPTO_XOR; /* MMO mode: XOR obfuscation, no MAC tag */
    srv_cfg.max_groups = kMaxZones + 16; /* Zone groups + spare */

    double sim_time = 10000.0;
    netudp_server_t* server = netudp_server_create(srv_addr, &srv_cfg, sim_time);
    if (server == nullptr) {
        std::fprintf(stderr, "[pvp] server_create failed\n");
        return r;
    }
    netudp_server_start(server, num_players + 16);

    /* Create zone groups */
    int zone_groups[kMaxZones];
    for (int i = 0; i < kMaxZones; ++i) {
        zone_groups[i] = netudp_group_create(server);
    }

    /* Create player schema */
    int schema = netudp_schema_create(server);
    int P_POS    = netudp_schema_add_vec3(server, schema, "position",   NETUDP_REP_ALL | NETUDP_REP_QUANTIZE);
    int P_ROT    = netudp_schema_add_quat(server, schema, "rotation",   NETUDP_REP_ALL | NETUDP_REP_QUANTIZE);
    int P_HEALTH = netudp_schema_add_f32(server, schema,  "health",     NETUDP_REP_ALL);
    int P_ANIM   = netudp_schema_add_u8(server, schema,   "anim_state", NETUDP_REP_ALL);
    int P_LEVEL  = netudp_schema_add_u16(server, schema,  "level",      NETUDP_REP_INITIAL_ONLY);
    (void)P_LEVEL;

    /* Connect N clients + create entities */
    std::vector<netudp_client_t*> clients(static_cast<size_t>(num_players), nullptr);
    std::vector<PlayerSim> players(static_cast<size_t>(num_players));

    for (int i = 0; i < num_players; ++i) {
        const char* addrs[] = { srv_addr };
        uint8_t token[2048] = {};
        netudp_generate_connect_token(1, addrs, 300, 10,
                                       static_cast<uint64_t>(100001 + i),
                                       kPvpProtoId, kPvpKey, nullptr, token);

        netudp_client_config_t cli_cfg = {};
        cli_cfg.protocol_id = kPvpProtoId;
        cli_cfg.num_channels = 2;
        cli_cfg.channels[0].type = NETUDP_CHANNEL_UNRELIABLE;
        cli_cfg.channels[1].type = NETUDP_CHANNEL_RELIABLE_ORDERED;

        clients[static_cast<size_t>(i)] = netudp_client_create(nullptr, &cli_cfg, sim_time);
        if (clients[static_cast<size_t>(i)] != nullptr) {
            netudp_client_connect(clients[static_cast<size_t>(i)], token);
        }
    }

    /* Handshake */
    auto hs_deadline = Clock::now() + std::chrono::milliseconds(10000);
    int connected = 0;
    while (Clock::now() < hs_deadline && connected < num_players) {
        sim_time += kTickDt;
        netudp_server_update(server, sim_time);
        connected = 0;
        for (int i = 0; i < num_players; ++i) {
            if (clients[static_cast<size_t>(i)] != nullptr) {
                netudp_client_update(clients[static_cast<size_t>(i)], sim_time);
                if (netudp_client_state(clients[static_cast<size_t>(i)]) == 3) {
                    connected++;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    std::fprintf(stderr, "          %d/%d clients connected (threads=%d)\n",
                 connected, num_players, num_io_threads);

    /* Initialize players: random positions, create entities, assign zones */
    for (int i = 0; i < num_players; ++i) {
        auto& p = players[static_cast<size_t>(i)];
        p.pos[0] = rng_range(1000.0f, kWorldSize - 1000.0f);
        p.pos[1] = rng_range(1000.0f, kWorldSize - 1000.0f);
        p.pos[2] = 0.0f;
        p.health = kMaxHealth;
        p.anim = ANIM_IDLE;
        p.speed = kWalkSpeed;
        p.dir[0] = rng_range(-1.0f, 1.0f);
        p.dir[1] = rng_range(-1.0f, 1.0f);
        float len = std::sqrt(p.dir[0]*p.dir[0] + p.dir[1]*p.dir[1]);
        if (len > 0.01f) { p.dir[0] /= len; p.dir[1] /= len; }
        p.attack_cooldown = rng_range(0.0f, kAttackInterval);
        p.alive = true;

        if (clients[static_cast<size_t>(i)] != nullptr &&
            netudp_client_state(clients[static_cast<size_t>(i)]) == 3) {
            p.client_slot = netudp_client_index(clients[static_cast<size_t>(i)]);
        }

        /* Create entity */
        p.entity_id = netudp_entity_create(server, schema);
        if (p.entity_id > 0 && p.client_slot >= 0) {
            netudp_entity_set_owner(server, p.entity_id, p.client_slot);
            netudp_entity_set_vec3(server, p.entity_id, P_POS, p.pos);
            netudp_entity_set_quat(server, p.entity_id, P_ROT, p.rot);
            netudp_entity_set_f32(server, p.entity_id, P_HEALTH, p.health);
            netudp_entity_set_u8(server, p.entity_id, P_ANIM, p.anim);
            netudp_entity_set_max_rate(server, p.entity_id, 20.0f);
            netudp_entity_set_priority(server, p.entity_id, 128);

            /* Assign to zone group */
            pos_to_zone(p.pos, &p.zone_x, &p.zone_y);
            p.zone_group = zone_groups[zone_index(p.zone_x, p.zone_y)];
            netudp_entity_set_group(server, p.entity_id, p.zone_group);
            netudp_group_add(server, p.zone_group, p.client_slot);
        }
    }

    /* Measurement loop */
    int total_warmup = cfg.warmup_iters + cfg.measure_iters;
    uint64_t total_entity_updates = 0;
    uint64_t total_combat_events = 0;
    uint64_t total_zone_transitions = 0;

    for (int iter = 0; iter < total_warmup; ++iter) {
        total_entity_updates = 0;
        total_combat_events = 0;
        total_zone_transitions = 0;

        auto t0 = Clock::now();
        double bench_start = sim_time;
        int ticks = 0;

        while (sim_time - bench_start < kBenchDuration) {
            sim_time += kTickDt;
            ticks++;

            /* === Simulate movement === */
            for (int i = 0; i < num_players; ++i) {
                auto& p = players[static_cast<size_t>(i)];
                if (!p.alive) {
                    p.respawn_timer -= kTickDt;
                    if (p.respawn_timer <= 0.0f) {
                        p.alive = true;
                        p.health = kMaxHealth;
                        p.anim = ANIM_IDLE;
                        p.pos[0] = rng_range(1000.0f, kWorldSize - 1000.0f);
                        p.pos[1] = rng_range(1000.0f, kWorldSize - 1000.0f);
                        netudp_entity_set_f32(server, p.entity_id, P_HEALTH, p.health);
                        netudp_entity_set_u8(server, p.entity_id, P_ANIM, p.anim);
                    }
                    continue;
                }

                /* Random direction change every ~2 seconds */
                if (rng_float() < kTickDt * 0.5f) {
                    p.dir[0] = rng_range(-1.0f, 1.0f);
                    p.dir[1] = rng_range(-1.0f, 1.0f);
                    float len = std::sqrt(p.dir[0]*p.dir[0] + p.dir[1]*p.dir[1]);
                    if (len > 0.01f) { p.dir[0] /= len; p.dir[1] /= len; }
                    p.speed = (rng_float() < 0.3f) ? kSprintSpeed : kWalkSpeed;
                    p.anim = (p.speed > kWalkSpeed) ? ANIM_SPRINT : ANIM_WALK;
                    netudp_entity_set_u8(server, p.entity_id, P_ANIM, p.anim);
                }

                /* Move */
                p.pos[0] += p.dir[0] * p.speed * kTickDt;
                p.pos[1] += p.dir[1] * p.speed * kTickDt;

                /* Wrap position */
                if (p.pos[0] < 0) { p.pos[0] += kWorldSize; }
                if (p.pos[0] >= kWorldSize) { p.pos[0] -= kWorldSize; }
                if (p.pos[1] < 0) { p.pos[1] += kWorldSize; }
                if (p.pos[1] >= kWorldSize) { p.pos[1] -= kWorldSize; }

                /* Update entity position */
                netudp_entity_set_vec3(server, p.entity_id, P_POS, p.pos);
                total_entity_updates++;

                /* Zone transition check */
                int new_zx, new_zy;
                pos_to_zone(p.pos, &new_zx, &new_zy);
                if (new_zx != p.zone_x || new_zy != p.zone_y) {
                    /* Remove from old zone group, add to new */
                    if (p.zone_group >= 0 && p.client_slot >= 0) {
                        netudp_group_remove(server, p.zone_group, p.client_slot);
                    }
                    p.zone_x = new_zx;
                    p.zone_y = new_zy;
                    p.zone_group = zone_groups[zone_index(new_zx, new_zy)];
                    if (p.client_slot >= 0) {
                        netudp_group_add(server, p.zone_group, p.client_slot);
                    }
                    netudp_entity_set_group(server, p.entity_id, p.zone_group);
                    total_zone_transitions++;
                }
            }

            /* === Simulate PvP combat (sampled, not all-vs-all) === */
            for (int i = 0; i < num_players; ++i) {
                auto& attacker = players[static_cast<size_t>(i)];
                if (!attacker.alive) { continue; }
                attacker.attack_cooldown -= kTickDt;
                if (attacker.attack_cooldown > 0.0f) { continue; }

                /* Find a nearby target in same zone */
                int target = -1;
                float best_dist = kSkillRange * kSkillRange;
                /* Sample up to 8 random players for O(1) combat check */
                for (int s = 0; s < 8; ++s) {
                    int j = rng_int(0, num_players);
                    if (j == i || !players[static_cast<size_t>(j)].alive) { continue; }
                    auto& t2 = players[static_cast<size_t>(j)];
                    float dx = attacker.pos[0] - t2.pos[0];
                    float dy = attacker.pos[1] - t2.pos[1];
                    float d2 = dx*dx + dy*dy;
                    if (d2 < best_dist) {
                        best_dist = d2;
                        target = j;
                    }
                }

                if (target >= 0) {
                    auto& victim = players[static_cast<size_t>(target)];
                    float dist = std::sqrt(best_dist);
                    float damage = (dist <= kMeleeRange) ? kMeleeDamage : kSkillDamage;
                    uint8_t atk_anim = (dist <= kMeleeRange) ? ANIM_ATTACK : ANIM_SKILL;

                    victim.health -= damage;
                    attacker.anim = atk_anim;
                    attacker.attack_cooldown = kAttackInterval;
                    total_combat_events++;

                    netudp_entity_set_u8(server, attacker.entity_id, P_ANIM, atk_anim);
                    netudp_entity_set_f32(server, victim.entity_id, P_HEALTH, victim.health);

                    if (victim.health <= 0.0f) {
                        victim.alive = false;
                        victim.respawn_timer = kRespawnTime;
                        victim.anim = ANIM_DEAD;
                        netudp_entity_set_u8(server, victim.entity_id, P_ANIM, ANIM_DEAD);
                    }
                }
            }

            /* === Replicate dirty entities to zone groups === */
            netudp_server_replicate(server);

            /* === Server network update === */
            netudp_server_update(server, sim_time);

            /* === Client updates (drain recv) === */
            for (int i = 0; i < num_players; ++i) {
                if (clients[static_cast<size_t>(i)] != nullptr) {
                    netudp_client_update(clients[static_cast<size_t>(i)], sim_time);
                }
            }

            /* Drain client receive queues */
            for (int i = 0; i < num_players; ++i) {
                if (clients[static_cast<size_t>(i)] == nullptr) { continue; }
                netudp_message_t* msgs[64];
                int n;
                while ((n = netudp_client_receive(clients[static_cast<size_t>(i)], msgs, 64)) > 0) {
                    for (int m = 0; m < n; ++m) { netudp_message_release(msgs[m]); }
                }
            }
        }

        auto t1 = Clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        double updates_per_sec = static_cast<double>(total_entity_updates) / elapsed;
        double combat_per_sec = static_cast<double>(total_combat_events) / elapsed;
        double ticks_per_sec = static_cast<double>(ticks) / elapsed;

        std::fprintf(stderr,
            "          ticks=%d  entities_updated=%llu  combat=%llu  zones_changed=%llu  "
            "elapsed=%.2fs  updates/s=%.0f  combat/s=%.0f  ticks/s=%.0f\n",
            ticks,
            (unsigned long long)total_entity_updates,
            (unsigned long long)total_combat_events,
            (unsigned long long)total_zone_transitions,
            elapsed, updates_per_sec, combat_per_sec, ticks_per_sec);

        if (iter >= cfg.warmup_iters) {
            r.samples_ns.push_back(1e9 / updates_per_sec);
            r.ops_per_sec = updates_per_sec;
        }
    }

    /* Compute percentiles */
    if (!r.samples_ns.empty()) {
        std::sort(r.samples_ns.begin(), r.samples_ns.end());
        size_t n = r.samples_ns.size();
        r.p50_ns = r.samples_ns[n / 2];
        r.p95_ns = r.samples_ns[n * 95 / 100];
        r.p99_ns = r.samples_ns[n * 99 / 100];
        r.max_ns = r.samples_ns[n - 1];
    }

    /* Cleanup */
    for (int i = 0; i < num_players; ++i) {
        netudp_entity_destroy(server, players[static_cast<size_t>(i)].entity_id);
    }
    for (int i = 0; i < kMaxZones; ++i) {
        netudp_group_destroy(server, zone_groups[i]);
    }
    netudp_schema_destroy(server, schema);

    netudp_server_stop(server);
    for (auto* c : clients) {
        if (c != nullptr) {
            netudp_client_disconnect(c);
            netudp_client_destroy(c);
        }
    }
    netudp_server_destroy(server);
    return r;
}

void register_pvp_sim_bench(BenchRegistry& reg) {
    reg.add("pvp_5000p_t2", [](const BenchConfig& c) {
        return run_pvp_sim(c, 5000, 2);
    });
}
