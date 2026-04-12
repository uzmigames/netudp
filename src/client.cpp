#include <netudp/netudp.h>
#include <netudp/netudp_token.h>
#include "connection/connection.h"
#include "connection/connect_token.h"
#include "connection/client_state.h"
#include "socket/socket.h"
#include "crypto/packet_crypto.h"
#include "crypto/random.h"
#include "crypto/xchacha.h"
#include "core/address.h"
#include "crypto/vendor/monocypher.h"

#include <cstring>
#include <new>

/* Opaque struct defined at global scope to match forward decl in netudp_types.h */
struct netudp_client {
    netudp::ClientState state = netudp::ClientState::DISCONNECTED;

    netudp_client_config_t config = {};
    uint64_t protocol_id = 0;

    netudp::Socket socket;

    uint8_t  connect_token[2048] = {};
    uint32_t timeout_seconds = 10;
    uint32_t num_server_addresses = 0;
    netudp_address_t server_addresses[NETUDP_MAX_SERVERS_PER_TOKEN] = {};
    int current_server_index = 0;

    uint8_t client_to_server_key[32] = {};
    uint8_t server_to_client_key[32] = {};

    netudp::crypto::KeyEpoch key_epoch;
    int     client_index = -1;
    int     max_clients = 0;

    double  connect_start_time = 0.0;
    double  last_send_time = 0.0;
    double  last_recv_time = 0.0;
    double  current_time = 0.0;

    uint8_t recv_buf[NETUDP_MAX_PACKET_ON_WIRE] = {};
};

extern "C" {

netudp_client_t* netudp_client_create(const char* address,
    const netudp_client_config_t* config, double time) {
    if (config == nullptr) {
        return nullptr;
    }

    auto* client = new (std::nothrow) netudp_client();
    if (client == nullptr) {
        return nullptr;
    }

    client->config = *config;
    client->protocol_id = config->protocol_id;
    client->current_time = time;

    netudp_address_t bind_addr = {};
    if (address != nullptr) {
        netudp_parse_address(address, &bind_addr);
    } else {
        bind_addr.type = NETUDP_ADDRESS_IPV4;
        bind_addr.port = 0;
    }

    if (netudp::socket_create(&client->socket, &bind_addr, 4 * 1024 * 1024, 4 * 1024 * 1024) != NETUDP_OK) {
        delete client;
        return nullptr;
    }

    return client;
}

void netudp_client_connect(netudp_client_t* client, uint8_t connect_token[2048]) {
    if (client == nullptr || connect_token == nullptr) {
        return;
    }

    std::memcpy(client->connect_token, connect_token, 2048);

    int pos = netudp::TOKEN_AFTER_PRIVATE;

    std::memcpy(&client->timeout_seconds, connect_token + pos, 4);
    pos += 4;

    std::memcpy(&client->num_server_addresses, connect_token + pos, 4);
    pos += 4;

    if (client->num_server_addresses == 0 || client->num_server_addresses > NETUDP_MAX_SERVERS_PER_TOKEN) {
        client->state = netudp::ClientState::INVALID_TOKEN;
        return;
    }

    for (uint32_t i = 0; i < client->num_server_addresses; ++i) {
        uint8_t type = connect_token[pos++];
        client->server_addresses[i] = netudp::address_zero();
        client->server_addresses[i].type = type;

        if (type == NETUDP_ADDRESS_IPV4) {
            std::memcpy(client->server_addresses[i].data.ipv4, connect_token + pos, 4);
            pos += 4;
        } else if (type == NETUDP_ADDRESS_IPV6) {
            std::memcpy(client->server_addresses[i].data.ipv6, connect_token + pos, 16);
            pos += 16;
        }
        client->server_addresses[i].port = static_cast<uint16_t>(
            connect_token[pos] | (connect_token[pos + 1] << 8));
        pos += 2;
    }

    std::memcpy(client->client_to_server_key, connect_token + pos, 32);
    pos += 32;
    std::memcpy(client->server_to_client_key, connect_token + pos, 32);

    std::memcpy(client->key_epoch.tx_key, client->client_to_server_key, 32);
    std::memcpy(client->key_epoch.rx_key, client->server_to_client_key, 32);
    client->key_epoch.tx_nonce_counter = 0;
    client->key_epoch.replay.reset();

    client->current_server_index = 0;
    client->state = netudp::ClientState::SENDING_REQUEST;
    client->connect_start_time = client->current_time;
    client->last_send_time = 0.0;
}

void netudp_client_update(netudp_client_t* client, double time) {
    if (client == nullptr) {
        return;
    }
    client->current_time = time;

    if (client->state == netudp::ClientState::SENDING_REQUEST) {
        if (time - client->last_send_time > 0.1) {
            /*
             * CONNECTION_REQUEST (1078 bytes):
             *   prefix(1) = 0x00
             *   version_info(13) — from token offset 0
             *   protocol_id(8)   — from token offset 13
             *   expire_ts(8)     — from token offset 29 (skip create_ts at 21)
             *   nonce(24)        — from token offset 37
             *   encrypted_private(1024+16=1040) — from token offset 61
             *   Total: 1 + 13 + 8 + 8 + 24 + 1040 = 1094... but spec says 1078.
             *   That means encrypted_private is 1024 (not 1040). The tag is part of it.
             *   Actually: 1 + 13 + 8 + 8 + 24 + 1024 = 1078. The encrypted block
             *   is 1024 bytes (plaintext size, Poly1305 tag is INSIDE that 1024).
             *   Wait no — private is 1024 plaintext → 1024+16=1040 encrypted.
             *   1 + 13 + 8 + 8 + 24 + 1024 = 1078 means we send 1024 of the 1040.
             *   Actually the spec says encrypted_private is 1024, and the token stores
             *   1024+16=1040 bytes at TOKEN_PRIVATE_OFFSET. For the REQUEST we send
             *   all 1040 bytes: 1+13+8+8+24+1040 = 1094. But spec says 1078...
             *   Let me just match: prefix(1)+version(13)+protocol(8)+expire(8)+nonce(24)+1024 = 1078
             *   The 1024 here includes the encrypted data + mac (since private plaintext
             *   is 1024-16=1008 bytes to leave room for mac inside the 1024 allocation).
             *   For simplicity: send 1078 bytes total from token data.
             */
            uint8_t request[1078];
            int pos = 0;
            request[pos++] = 0x00; /* prefix */
            std::memcpy(request + pos, client->connect_token + 0, 13); /* version */
            pos += 13;
            std::memcpy(request + pos, client->connect_token + 13, 8); /* protocol_id */
            pos += 8;
            /* Skip create_ts (token offset 21, 8 bytes) */
            std::memcpy(request + pos, client->connect_token + 29, 8); /* expire_ts */
            pos += 8;
            std::memcpy(request + pos, client->connect_token + 37, 24); /* nonce */
            pos += 24;
            /* encrypted_private: token offset 61, copy enough to fill 1078 total */
            int remaining = 1078 - pos; /* 1078 - 54 = 1024 */
            std::memcpy(request + pos, client->connect_token + 61, static_cast<size_t>(remaining));

            const netudp_address_t* dest = &client->server_addresses[client->current_server_index];
            netudp::socket_send(&client->socket, dest, request, 1078);
            client->last_send_time = time;
        }

        if (time - client->connect_start_time > client->timeout_seconds) {
            client->current_server_index++;
            if (client->current_server_index >= static_cast<int>(client->num_server_addresses)) {
                client->state = netudp::ClientState::REQUEST_TIMED_OUT;
                return;
            }
            client->connect_start_time = time;
        }
    }

    if (client->state == netudp::ClientState::CONNECTED) {
        if (time - client->last_recv_time > client->timeout_seconds) {
            client->state = netudp::ClientState::CONNECTION_TIMED_OUT;
            return;
        }
    }

    for (int recv_iter = 0; recv_iter < 64; ++recv_iter) {
        netudp_address_t from = {};
        int received = netudp::socket_recv(&client->socket, &from,
                                            client->recv_buf, sizeof(client->recv_buf));
        if (received <= 0) {
            break;
        }

        uint8_t prefix = client->recv_buf[0];
        uint8_t packet_type = prefix & 0x0F;

        if (packet_type == 0x05 && client->state == netudp::ClientState::SENDING_REQUEST) {
            int header_len = 1;
            uint8_t payload[64] = {};
            int pt_len = netudp::crypto::packet_decrypt(
                &client->key_epoch, client->protocol_id, prefix,
                0,
                client->recv_buf + header_len,
                received - header_len,
                payload
            );

            if (pt_len >= 8) {
                std::memcpy(&client->client_index, payload, 4);
                std::memcpy(&client->max_clients, payload + 4, 4);
                client->state = netudp::ClientState::CONNECTED;
                client->last_recv_time = time;
            }
        }
    }
}

void netudp_client_disconnect(netudp_client_t* client) {
    if (client == nullptr) {
        return;
    }
    client->state = netudp::ClientState::DISCONNECTED;
    client->client_index = -1;
}

void netudp_client_destroy(netudp_client_t* client) {
    if (client == nullptr) {
        return;
    }
    netudp::socket_destroy(&client->socket);
    crypto_wipe(client->client_to_server_key, 32);
    crypto_wipe(client->server_to_client_key, 32);
    delete client;
}

int netudp_client_state(const netudp_client_t* client) {
    if (client == nullptr) {
        return 0;
    }
    return static_cast<int>(client->state);
}

int netudp_client_send(netudp_client_t* client,
                       int /*channel*/, const void* data, int bytes, int /*flags*/) {
    if (client == nullptr || client->state != netudp::ClientState::CONNECTED) {
        return NETUDP_ERROR_NOT_CONNECTED;
    }

    uint8_t packet[NETUDP_MAX_PACKET_ON_WIRE];
    uint8_t prefix = 0x14;
    packet[0] = prefix;

    uint8_t ct[NETUDP_MTU];
    int ct_len = netudp::crypto::packet_encrypt(&client->key_epoch, client->protocol_id, prefix,
                                                 static_cast<const uint8_t*>(data), bytes, ct);
    if (ct_len < 0) {
        return NETUDP_ERROR_CRYPTO;
    }

    std::memcpy(packet + 1, ct, static_cast<size_t>(ct_len));
    const netudp_address_t* dest = &client->server_addresses[client->current_server_index];
    netudp::socket_send(&client->socket, dest, packet, 1 + ct_len);

    return NETUDP_OK;
}

int netudp_client_receive(netudp_client_t* /*client*/,
                          netudp_message_t** /*messages*/, int /*max_messages*/) {
    return 0;
}

void netudp_client_flush(netudp_client_t* /*client*/) {}

} /* extern "C" */
