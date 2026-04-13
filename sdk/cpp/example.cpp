/**
 * @file example.cpp
 * @brief C++ SDK usage example — echo server + client in one file.
 *
 * Build: zig c++ -std=c++17 -I../../include -I. example.cpp -L../../build -lnetudp -o example
 */

#include "netudp.hpp"
#include <cstdio>
#include <chrono>
#include <thread>

int main() {
    netudp::Init guard;

    /* Generate a shared key (in production, use a real key from your backend) */
    uint8_t key[32] = {};
    for (int i = 0; i < 32; ++i) { key[i] = static_cast<uint8_t>(i + 1); }

    /* Server config */
    netudp::ServerConfig srv_cfg;
    srv_cfg.protocol_id(0x1234567890ABCDEF)
           .private_key(key)
           .channels({{netudp::Channel::Unreliable},
                      {netudp::Channel::ReliableOrdered}});

    /* Create server */
    netudp::Server server("127.0.0.1:27015", srv_cfg, 0.0);
    if (!server) {
        std::printf("Failed to create server\n");
        return 1;
    }
    server.start(64);
    std::printf("Server started on 127.0.0.1:27015 (max %d clients)\n",
                server.max_clients());

    /* Generate connect token */
    uint8_t token[2048] = {};
    netudp::generate_connect_token(
        {"127.0.0.1:27015"}, 300, 10,
        42, 0x1234567890ABCDEF, key, nullptr, token);

    /* Client config */
    netudp::ClientConfig cli_cfg;
    cli_cfg.protocol_id(0x1234567890ABCDEF)
           .channels({{netudp::Channel::Unreliable},
                      {netudp::Channel::ReliableOrdered}});

    /* Create client */
    netudp::Client client(nullptr, cli_cfg, 0.0);
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
            /* Send a position update via zero-copy buffer */
            auto buf = server.acquire_buffer();
            buf.u8(0x01).f32(1.0f).f32(2.0f).f32(3.0f);
            server.send_buffer(0, 0, buf);

            /* Or send raw bytes */
            uint8_t ping[] = {0x02, 0x00};
            client.send(0, ping, sizeof(ping));
        }

        /* Receive on server */
        server.receive_all(64, [](netudp::Message msg) {
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

    std::printf("Done. Server stats: %d connected, %.0f pps in\n",
                server.stats().connected_clients, server.stats().recv_pps);
    return 0;
}
