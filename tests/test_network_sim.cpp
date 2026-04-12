#include <gtest/gtest.h>
#include "../src/sim/network_sim.h"
#include <cstring>
#include <memory>

/* ======================================================================
 * Helpers — kept small to avoid stack pressure
 * ====================================================================== */

/* Maximum packets collected per test — tests send at most 20 packets. */
static constexpr int kMaxCollect = 32;

struct DeliveredPacket {
    uint8_t          data[1500];
    int              len;
    netudp_address_t from;
};

struct CollectCtx {
    DeliveredPacket packets[kMaxCollect];
    int             count;
};

static void collect_cb(void* ctx, const uint8_t* data, int len,
                        const netudp_address_t* from) {
    auto* c = static_cast<CollectCtx*>(ctx);
    if (c->count >= kMaxCollect) {
        return;
    }
    int copy_len = len < 1500 ? len : 1500;
    std::memcpy(c->packets[c->count].data, data, static_cast<size_t>(copy_len));
    c->packets[c->count].len  = copy_len;
    c->packets[c->count].from = *from;
    ++c->count;
}

static netudp_address_t make_addr() {
    netudp_address_t addr = {};
    addr.type         = NETUDP_ADDRESS_IPV4;
    addr.data.ipv4[0] = 127;
    addr.data.ipv4[3] = 1;
    addr.port         = 12345;
    return addr;
}

/* ======================================================================
 * Tests — NetworkSimulator heap-allocated to avoid ~784 KB stack frame
 * ====================================================================== */

TEST(NetworkSim, HundredPercentLossDropsAll) {
    netudp::NetSimConfig cfg;
    cfg.loss_percent = 100.0F;

    auto sim = std::make_unique<netudp::NetworkSimulator>();
    sim->init(cfg);

    netudp_address_t addr = make_addr();
    uint8_t pkt[32] = {};

    for (int i = 0; i < 100; ++i) {
        sim->submit(pkt, static_cast<int>(sizeof(pkt)), &addr, 0.0);
    }

    CollectCtx ctx = {};
    sim->poll(1.0, &ctx, collect_cb);

    EXPECT_EQ(ctx.count, 0);
}

TEST(NetworkSim, ZeroLossPassesAll) {
    netudp::NetSimConfig cfg;
    /* All defaults: 0% loss, 0ms latency */

    auto sim = std::make_unique<netudp::NetworkSimulator>();
    sim->init(cfg);

    netudp_address_t addr = make_addr();
    uint8_t pkt[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    for (int i = 0; i < 10; ++i) {
        sim->submit(pkt, static_cast<int>(sizeof(pkt)), &addr, 0.0);
    }

    CollectCtx ctx = {};
    sim->poll(1.0, &ctx, collect_cb);

    EXPECT_EQ(ctx.count, 10);
    for (int i = 0; i < ctx.count; ++i) {
        EXPECT_EQ(ctx.packets[i].len, 8);
        EXPECT_EQ(std::memcmp(ctx.packets[i].data, pkt, 8), 0);
    }
}

TEST(NetworkSim, FiftyMsLatencyDelaysCorrectly) {
    netudp::NetSimConfig cfg;
    cfg.latency_min_ms = 50.0F;
    cfg.latency_max_ms = 50.0F; /* Fixed 50ms, no jitter */

    auto sim = std::make_unique<netudp::NetworkSimulator>();
    sim->init(cfg);

    netudp_address_t addr = make_addr();
    uint8_t pkt[4] = {0xAB};

    sim->submit(pkt, 4, &addr, 0.0);

    /* Poll at 40ms — packet must NOT be delivered yet */
    CollectCtx early = {};
    sim->poll(0.040, &early, collect_cb);
    EXPECT_EQ(early.count, 0) << "Packet delivered before delay expired";

    /* Poll at 60ms — packet must be delivered now */
    CollectCtx late = {};
    sim->poll(0.060, &late, collect_cb);
    EXPECT_EQ(late.count, 1) << "Packet not delivered after delay expired";
}

TEST(NetworkSim, HundredPercentDuplicateGeneratesTwoCopies) {
    netudp::NetSimConfig cfg;
    cfg.duplicate_percent = 100.0F;
    cfg.latency_min_ms    = 0.0F;
    cfg.latency_max_ms    = 0.0F;

    auto sim = std::make_unique<netudp::NetworkSimulator>();
    sim->init(cfg);

    netudp_address_t addr = make_addr();
    uint8_t pkt[4] = {0x42};

    int inserted = sim->submit(pkt, 4, &addr, 0.0);
    EXPECT_EQ(inserted, 2) << "submit() should return 2 for a duplicated packet";

    CollectCtx ctx = {};
    sim->poll(1.0, &ctx, collect_cb);
    EXPECT_EQ(ctx.count, 2) << "poll() should deliver 2 copies of a duplicated packet";
}
