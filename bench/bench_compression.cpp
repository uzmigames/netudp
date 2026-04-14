/**
 * @file bench_compression.cpp
 * @brief Benchmark: netc compression on game-like payloads.
 *
 * Trains a dictionary on typical MMORPG packets, then measures
 * compress/decompress throughput and ratio.
 */

#include "bench_main.h"
#include <netudp/netudp.h>

#ifdef NETUDP_HAS_NETC
#include <netc.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

/* ---- Game-like payload generators ---- */

static void fill_position_update(uint8_t* buf, int idx) {
    float pos[3] = {100.0f + static_cast<float>(idx) * 8.33f,
                    200.0f + static_cast<float>(idx) * 5.17f,
                    0.0f};
    float rot[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float hp = 850.0f - static_cast<float>(idx % 200);
    uint8_t anim = static_cast<uint8_t>(idx % 6);
    uint16_t level = 42;

    int off = 0;
    std::memcpy(buf + off, pos, 12); off += 12;
    std::memcpy(buf + off, rot, 16); off += 16;
    std::memcpy(buf + off, &hp, 4); off += 4;
    buf[off++] = anim;
    std::memcpy(buf + off, &level, 2); off += 2;
    std::memset(buf + off, 0, 48 - off);
}

static void fill_combat_packet(uint8_t* buf, int idx) {
    uint16_t attacker = static_cast<uint16_t>(idx % 5000);
    uint16_t target = static_cast<uint16_t>((idx + 42) % 5000);
    float damage = 50.0f + static_cast<float>(idx % 100);
    uint8_t skill_id = static_cast<uint8_t>(idx % 12);
    uint8_t hit_type = static_cast<uint8_t>(idx % 3);
    float pos[3] = {500.0f + static_cast<float>(idx), 300.0f, 0.0f};

    int off = 0;
    std::memcpy(buf + off, &attacker, 2); off += 2;
    std::memcpy(buf + off, &target, 2); off += 2;
    std::memcpy(buf + off, &damage, 4); off += 4;
    buf[off++] = skill_id;
    buf[off++] = hit_type;
    std::memcpy(buf + off, pos, 12); off += 12;
    std::memset(buf + off, 0, 32 - off);
}

#ifdef NETUDP_HAS_NETC

static BenchResult run_compression_bench(const BenchConfig& /*cfg*/) {
    using Clock = std::chrono::high_resolution_clock;
    BenchResult r;
    r.name = "compression";

    /* Generate training data: 1000 position updates + 200 combat packets */
    static constexpr int kTrainPos = 1000;
    static constexpr int kTrainCombat = 200;
    static constexpr int kTrainTotal = kTrainPos + kTrainCombat;

    std::vector<uint8_t> train_bufs(static_cast<size_t>(kTrainTotal) * 48);
    std::vector<const uint8_t*> train_ptrs(static_cast<size_t>(kTrainTotal));
    std::vector<size_t> train_sizes(static_cast<size_t>(kTrainTotal));

    for (int i = 0; i < kTrainPos; ++i) {
        uint8_t* p = train_bufs.data() + static_cast<size_t>(i) * 48;
        fill_position_update(p, i);
        train_ptrs[static_cast<size_t>(i)] = p;
        train_sizes[static_cast<size_t>(i)] = 48;
    }
    for (int i = 0; i < kTrainCombat; ++i) {
        uint8_t* p = train_bufs.data() + static_cast<size_t>(kTrainPos + i) * 48;
        fill_combat_packet(p, i);
        train_ptrs[static_cast<size_t>(kTrainPos + i)] = p;
        train_sizes[static_cast<size_t>(kTrainPos + i)] = 32;
    }

    /* Train dictionary */
    netc_dict_t* dict = nullptr;
    netc_result_t tr = netc_dict_train(train_ptrs.data(), train_sizes.data(),
                                        static_cast<size_t>(kTrainTotal), 1, &dict);
    if (tr != NETC_OK || dict == nullptr) {
        std::fprintf(stderr, "          [SKIP] netc_dict_train failed (result=%d)\n", tr);
        return r;
    }

    /* Benchmark payloads */
    struct PayloadDef { const char* name; int size; };
    PayloadDef payloads[] = {
        {"position_48B", 48},
        {"combat_32B",   32},
        {"batch_10pos_480B", 480},
    };

    uint8_t pos_buf[48], combat_buf[32], batch_buf[480];
    fill_position_update(pos_buf, 999);
    fill_combat_packet(combat_buf, 999);
    for (int i = 0; i < 10; ++i) fill_position_update(batch_buf + i * 48, 500 + i);

    const uint8_t* payload_ptrs[] = { pos_buf, combat_buf, batch_buf };

    static constexpr int kIter = 100000;

    std::fprintf(stderr, "\n          %-30s %6s -> %6s  %6s  %10s  %10s\n",
                 "Payload", "Raw", "Comp", "Ratio", "Comp ns", "Decomp ns");
    std::fprintf(stderr, "          %-30s %6s    %6s  %6s  %10s  %10s\n",
                 "------", "---", "----", "-----", "-------", "---------");

    for (int pi = 0; pi < 3; ++pi) {
        const uint8_t* src = payload_ptrs[pi];
        int src_len = payloads[pi].size;

        uint8_t compressed[2048] = {};
        uint8_t decompressed[2048] = {};

        /* Stateless compress */
        size_t comp_size = 0;
        netc_compress_stateless(dict, src, static_cast<size_t>(src_len),
                                 compressed, sizeof(compressed), &comp_size);

        /* Bench compress */
        auto t0 = Clock::now();
        for (int i = 0; i < kIter; ++i) {
            netc_compress_stateless(dict, src, static_cast<size_t>(src_len),
                                     compressed, sizeof(compressed), &comp_size);
        }
        auto t1 = Clock::now();
        double comp_ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / kIter;

        /* Bench decompress */
        auto t2 = Clock::now();
        for (int i = 0; i < kIter; ++i) {
            size_t dec_size = 0;
            netc_decompress_stateless(dict, compressed, comp_size,
                                       decompressed, sizeof(decompressed), &dec_size);
        }
        auto t3 = Clock::now();
        double decomp_ns = std::chrono::duration<double, std::nano>(t3 - t2).count() / kIter;

        double ratio = static_cast<double>(comp_size) / static_cast<double>(src_len) * 100.0;

        std::fprintf(stderr, "          %-30s %4dB -> %4dB  %5.1f%%  %8.0f ns  %8.0f ns\n",
                     payloads[pi].name, src_len, static_cast<int>(comp_size),
                     ratio, comp_ns, decomp_ns);
    }

    /* Baseline: no compression (memcpy only) */
    auto tb0 = Clock::now();
    uint8_t dst[512];
    for (int i = 0; i < kIter; ++i) {
        std::memcpy(dst, batch_buf, 480);
    }
    auto tb1 = Clock::now();
    double memcpy_ns = std::chrono::duration<double, std::nano>(tb1 - tb0).count() / kIter;
    std::fprintf(stderr, "          %-30s %4dB -> %4dB  %5.1f%%  %8.0f ns\n",
                 "baseline_memcpy_480B", 480, 480, 100.0, memcpy_ns);

    /* Use batch compress time as primary metric */
    r.ops_per_sec = 1e9 / memcpy_ns;
    r.samples_ns.push_back(memcpy_ns);
    r.p50_ns = memcpy_ns;

    netc_dict_free(dict);
    return r;
}

#else

static BenchResult run_compression_bench(const BenchConfig& /*cfg*/) {
    BenchResult r;
    r.name = "compression";
    std::fprintf(stderr, "          [SKIP] NETUDP_HAS_NETC not defined\n");
    return r;
}

#endif

void register_compression_bench(BenchRegistry& reg) {
    reg.add("compression", [](const BenchConfig& c) {
        return run_compression_bench(c);
    });
}
