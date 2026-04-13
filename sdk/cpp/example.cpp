/**
 * @file example.cpp
 * @brief C++ SDK usage example — typed packets, dispatch, batching.
 *
 * Build: zig c++ -std=c++17 -I../../include -I. example.cpp -L../../build/release -lnetudp -o example
 */

#include "netudp.hpp"
#include <cstdio>
#include <chrono>
#include <thread>

/* ======================================================================
 * Define game packets as structs (ToS-Server-5 pattern)
 *
 * Each packet has:
 *   - static constexpr uint8_t packet_type
 *   - void serialize(BufferWriter&) const
 *   - static T deserialize(RawReader&)
 * ====================================================================== */

struct UpdateEntity {
    static constexpr uint8_t packet_type = 0x01;

    uint32_t entity_id;
    float    pos_x, pos_y, pos_z;
    float    rot_y;
    uint16_t anim_state;

    void serialize(netudp::BufferWriter& buf) const {
        buf.u8(packet_type)
           .u32(entity_id)
           .vec3(pos_x, pos_y, pos_z)   /* 6 bytes quantized */
           .rot(rot_y)                   /* 1 byte */
           .u16(anim_state);
    }

    static UpdateEntity deserialize(netudp::RawReader& r) {
        float x = 0, y = 0, z = 0;
        r.vec3(x, y, z);
        return {r.u32(), x, y, z, r.rot(), r.u16()};
    }
};

struct ChatMessage {
    static constexpr uint8_t packet_type = 0x02;

    uint32_t    sender_id;
    const char* text;

    void serialize(netudp::BufferWriter& buf) const {
        buf.u8(packet_type).u32(sender_id).string(text);
    }

    /* ChatMessage deserialization would need string handling —
     * for this example we use raw handler instead */
};

struct HealthUpdate {
    static constexpr uint8_t packet_type = 0x03;

    uint32_t entity_id;
    uint16_t hp;
    uint16_t max_hp;

    void serialize(netudp::BufferWriter& buf) const {
        buf.u8(packet_type).u32(entity_id).u16(hp).u16(max_hp);
    }

    static HealthUpdate deserialize(netudp::RawReader& r) {
        return {r.u32(), r.u16(), r.u16()};
    }
};

/* ====================================================================== */

int main() {
    netudp::Init guard;

    /* Logging + profiling */
    netudp::set_log_level(netudp::LogLevel::Info);
    netudp::profiling_enable();

    /* Create server — key auto-generated, handshake/ping automatic */
    netudp::Server server("127.0.0.1:27015", "my-mmorpg-v1", 1024);
    if (!server) { return 1; }

    /* Callbacks */
    server.on_connect([](int client, uint64_t id) {
        std::printf("[+] Client %d connected (id=%llu)\n",
                    client, static_cast<unsigned long long>(id));
    });
    server.on_disconnect([](int client, int reason) {
        std::printf("[-] Client %d disconnected (%d)\n", client, reason);
    });

    /* Type-safe packet dispatch */
    netudp::PacketDispatcher dispatch;

    dispatch.on<UpdateEntity>([](int client, UpdateEntity pkt) {
        std::printf("  Entity %u moved to (%.1f, %.1f, %.1f) anim=%u\n",
                    pkt.entity_id, pkt.pos_x, pkt.pos_y, pkt.pos_z,
                    pkt.anim_state);
        (void)client;
    });

    dispatch.on<HealthUpdate>([](int client, HealthUpdate pkt) {
        std::printf("  Entity %u HP: %u/%u\n",
                    pkt.entity_id, pkt.hp, pkt.max_hp);
        (void)client;
    });

    dispatch.on_raw(ChatMessage::packet_type, [](int client, const uint8_t* data, int size) {
        std::printf("  Chat from client %d: %d bytes\n", client, size);
        (void)data;
    });

    /* Generate token + create client */
    uint8_t token[2048] = {};
    server.generate_token(42, "127.0.0.1:27015", token);

    netudp::Client client("my-mmorpg-v1");
    client.connect(token);

    /* Game loop */
    double time = 0.0;
    for (int tick = 0; tick < 200; ++tick) {
        time += 0.016;
        server.update(time);
        client.update(time);

        if (client.connected() && tick % 10 == 0) {
            /* Batch multiple packets into one buffer (coalesced into 1 UDP packet) */
            auto buf = server.acquire_buffer();
            UpdateEntity{42, 100.5f, 200.3f, 50.1f, 1.57f, 3}.serialize(buf);
            HealthUpdate{42, 85, 100}.serialize(buf);
            server.send_buffer(0, 0, buf);
        }

        /* Receive + dispatch */
        server.receive_all(64, [&](netudp::Message msg) {
            dispatch.handle(msg);
        });

        client.receive(64, [](netudp::Message msg) {
            std::printf("Client got %d bytes on ch%d\n", msg.size(), msg.channel());
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    /* Profiling report */
    auto zones = netudp::profiling_get_zones();
    std::printf("\n=== Profiling (%zu zones) ===\n", zones.size());
    for (auto& z : zones) {
        std::printf("  %-30s  calls=%llu  avg=%.0f ns\n",
                    z.name, static_cast<unsigned long long>(z.call_count), z.avg_ns());
    }

    return 0;
}
