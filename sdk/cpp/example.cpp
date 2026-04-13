/**
 * @file example.cpp
 * @brief C++ SDK usage example — echo server + client.
 *
 * Build: zig c++ -std=c++17 -I../../include -I. example.cpp -L../../build/release -lnetudp -o example
 */

#include "netudp.hpp"
#include <cstdio>
#include <chrono>
#include <thread>

int main() {
    netudp::Init guard;

    /* Optional: configure logging and profiling */
    netudp::set_log_level(netudp::LogLevel::Info);
    netudp::profiling_enable();

    /* Create server — key auto-generated, ping/keepalive/handshake automatic */
    netudp::Server server("127.0.0.1:27015", "my-game-v1", 64);
    if (!server) {
        std::printf("Failed to create server\n");
        return 1;
    }

    std::printf("Server started (max %d clients, protocol=0x%llx)\n",
                server.max_clients(),
                static_cast<unsigned long long>(server.get_protocol_id()));

    /* Register callbacks */
    server.on_connect([](int client, uint64_t id) {
        std::printf("Client %d connected (id=%llu)\n",
                    client, static_cast<unsigned long long>(id));
    });

    server.on_disconnect([](int client, int reason) {
        std::printf("Client %d disconnected (reason=%d)\n", client, reason);
    });

    /* Generate a connect token for client ID 42 */
    uint8_t token[2048] = {};
    server.generate_token(42, "127.0.0.1:27015", token);

    /* Create client — game_name must match server */
    netudp::Client client("my-game-v1");
    if (!client) {
        std::printf("Failed to create client\n");
        return 1;
    }
    client.connect(token);

    /* Game loop */
    double time = 0.0;
    for (int tick = 0; tick < 200; ++tick) {
        time += 0.016;
        server.update(time);
        client.update(time);

        if (client.connected() && tick % 10 == 0) {
            /* Send via zero-copy buffer (fluent API) */
            auto buf = server.acquire_buffer();
            buf.u8(0x01).f32(1.0f).f32(2.0f).f32(3.0f);
            server.send_buffer(0, 0, buf);

            /* Or send raw bytes */
            uint8_t ping[] = {0x02, 0x00};
            client.send_unreliable(ping, sizeof(ping));
        }

        /* Receive on server (zero-allocation callback) */
        server.receive_all(64, [&](netudp::Message msg) {
            std::printf("Server got %d bytes on ch%d from client %d\n",
                        msg.size(), msg.channel(), msg.client());
        });

        /* Receive on client */
        client.receive(64, [](netudp::Message msg) {
            std::printf("Client got %d bytes on ch%d\n",
                        msg.size(), msg.channel());
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    /* Print profiling zones */
    auto zones = netudp::profiling_get_zones();
    std::printf("\n=== Profiling (%zu zones) ===\n", zones.size());
    for (auto& z : zones) {
        std::printf("  %-30s  calls=%llu  avg=%.0f ns\n",
                    z.name,
                    static_cast<unsigned long long>(z.call_count),
                    z.avg_ns());
    }

    auto s = server.stats();
    std::printf("\nServer stats: %d connected, %.0f pps in\n",
                s.connected_clients, s.recv_pps);
    return 0;
}
