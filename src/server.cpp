#include <netudp/netudp.h>
#include <netudp/netudp_token.h>
#include "connection/connection.h"
#include "connection/connect_token.h"
#include "connection/rate_limiter.h"
#include "connection/client_state.h"
#include "socket/socket.h"
#include "crypto/packet_crypto.h"
#include "crypto/random.h"
#include "core/address.h"
#include "core/allocator.h"
#include "crypto/xchacha.h"
#include "crypto/vendor/monocypher.h"

#include <cstring>
#include <ctime>
#include <new>

struct netudp_server {
    bool running = false;
    int  max_clients = 0;

    netudp_server_config_t config = {};
    uint64_t protocol_id = 0;

    netudp::Socket socket;
    netudp::Allocator allocator;
    netudp::RateLimiter rate_limiter;

    netudp::Connection* connections = nullptr;

    uint8_t challenge_key[32] = {};
    uint64_t challenge_sequence = 0;

    struct FingerprintEntry {
        netudp::TokenFingerprint fingerprint;
        netudp_address_t address;
        uint64_t         expire_time;
        bool             used;
    };
    FingerprintEntry fingerprint_cache[1024] = {};
    int fingerprint_count = 0;

    double current_time = 0.0;
    double last_cleanup_time = 0.0;

    uint8_t recv_buf[NETUDP_MAX_PACKET_ON_WIRE] = {};
};

namespace netudp {
void server_handle_connection_request(netudp_server* server,
    const netudp_address_t* from, const uint8_t* packet, int packet_len);
}

extern "C" {

using namespace netudp;

netudp_server_t* netudp_server_create(const char* address,
    const netudp_server_config_t* config, double time) {
    if (address == nullptr || config == nullptr) {
        return nullptr;
    }

    auto* server = new (std::nothrow) netudp_server();
    if (server == nullptr) {
        return nullptr;
    }

    server->config = *config;
    server->protocol_id = config->protocol_id;
    server->current_time = time;

    /* Setup allocator */
    server->allocator.context = config->allocator_context;
    server->allocator.alloc = config->allocate_function;
    server->allocator.free = config->free_function;

    /* Parse bind address */
    netudp_address_t bind_addr = {};
    if (netudp_parse_address(address, &bind_addr) != NETUDP_OK) {
        delete server;
        return nullptr;
    }

    /* Create socket */
    if (socket_create(&server->socket, &bind_addr, 4 * 1024 * 1024, 4 * 1024 * 1024) != NETUDP_OK) {
        delete server;
        return nullptr;
    }

    return server;
}

void netudp_server_start(netudp_server_t* server, int max_clients) {
    if (server == nullptr || max_clients <= 0) {
        return;
    }

    server->max_clients = max_clients;
    server->running = true;

    /* Allocate connection slots */
    server->connections = static_cast<Connection*>(
        server->allocator.allocate(sizeof(Connection) * static_cast<size_t>(max_clients)));
    if (server->connections != nullptr) {
        for (int i = 0; i < max_clients; ++i) {
            new (&server->connections[i]) Connection();
        }
    }

    /* Generate challenge key */
    crypto::random_bytes(server->challenge_key, 32);
    server->challenge_sequence = 0;
}

void netudp_server_stop(netudp_server_t* server) {
    if (server == nullptr) {
        return;
    }
    server->running = false;

    if (server->connections != nullptr) {
        server->allocator.deallocate(server->connections);
        server->connections = nullptr;
    }
}

void netudp_server_update(netudp_server_t* server, double time) {
    if (server == nullptr || !server->running) {
        return;
    }

    server->current_time = time;

    /* Receive packets */
    for (int recv_iter = 0; recv_iter < 1024; ++recv_iter) {
        netudp_address_t from = {};
        int received = socket_recv(&server->socket, &from,
                                    server->recv_buf, sizeof(server->recv_buf));
        if (received <= 0) {
            break;
        }

        /* Rate limit check */
        if (!server->rate_limiter.allow(&from, time)) {
            continue;
        }

        uint8_t prefix = server->recv_buf[0];
        uint8_t packet_type = prefix & 0x0F;

        if (packet_type == 0x00 && received == 1078) {
            /* CONNECTION_REQUEST */
            server_handle_connection_request(server, &from, server->recv_buf, received);
        }
        /* Other packet types handled in later phases */
    }

    /* Periodic cleanup */
    if (time - server->last_cleanup_time > 1.0) {
        server->rate_limiter.cleanup(time);

        /* Check connection timeouts */
        for (int i = 0; i < server->max_clients; ++i) {
            Connection& conn = server->connections[i];
            if (conn.active && time - conn.last_recv_time > conn.timeout_seconds) {
                /* Disconnect timed-out client */
                if (server->config.on_disconnect != nullptr) {
                    server->config.on_disconnect(server->config.callback_context, i, -4);
                }
                conn.reset();
            }
        }

        server->last_cleanup_time = time;
    }
}

void netudp_server_destroy(netudp_server_t* server) {
    if (server == nullptr) {
        return;
    }

    if (server->running) {
        netudp_server_stop(server);
    }

    socket_destroy(&server->socket);

    /* Wipe sensitive data */
    crypto_wipe(server->challenge_key, sizeof(server->challenge_key));

    delete server;
}

/* --- Send/Receive (functional stubs — will be fleshed out in Phase 3) --- */

int netudp_server_send(netudp_server_t* server, int client_index,
                       int /*channel*/, const void* data, int bytes, int /*flags*/) {
    if (server == nullptr || !server->running) {
        return NETUDP_ERROR_NOT_INITIALIZED;
    }
    if (client_index < 0 || client_index >= server->max_clients) {
        return NETUDP_ERROR_INVALID_PARAM;
    }
    Connection& conn = server->connections[client_index];
    if (!conn.active) {
        return NETUDP_ERROR_NOT_CONNECTED;
    }

    /* For now, encrypt and send directly (no channels/fragmentation yet) */
    uint8_t packet[NETUDP_MAX_PACKET_ON_WIRE];
    uint8_t prefix = 0x14; /* DATA, 1-byte seq */
    packet[0] = prefix;
    /* Simplified: no variable seq or connection_id yet */
    int header_len = 1;

    uint8_t ct[NETUDP_MTU];
    int ct_len = crypto::packet_encrypt(&conn.key_epoch, server->protocol_id, prefix,
                                         static_cast<const uint8_t*>(data), bytes, ct);
    if (ct_len < 0) {
        return NETUDP_ERROR_CRYPTO;
    }

    std::memcpy(packet + header_len, ct, static_cast<size_t>(ct_len));
    int total = header_len + ct_len;

    socket_send(&server->socket, &conn.address, packet, total);
    conn.last_send_time = server->current_time;

    return NETUDP_OK;
}

int netudp_server_receive(netudp_server_t* /*server*/, int /*client_index*/,
                          netudp_message_t** /*messages*/, int /*max_messages*/) {
    return 0; /* Phase 3: channel-based receive */
}



void netudp_server_broadcast(netudp_server_t* server, int channel,
                             const void* data, int bytes, int flags) {
    if (server == nullptr || !server->running) {
        return;
    }
    for (int i = 0; i < server->max_clients; ++i) {
        if (server->connections[i].active) {
            netudp_server_send(server, i, channel, data, bytes, flags);
        }
    }
}

void netudp_server_broadcast_except(netudp_server_t* server, int except_client,
                                    int channel, const void* data, int bytes, int flags) {
    if (server == nullptr || !server->running) {
        return;
    }
    for (int i = 0; i < server->max_clients; ++i) {
        if (i != except_client && server->connections[i].active) {
            netudp_server_send(server, i, channel, data, bytes, flags);
        }
    }
}

void netudp_server_flush(netudp_server_t* /*server*/, int /*client_index*/) {}

int netudp_server_connection_count(netudp_server_t* server) {
    if (server == nullptr || !server->running) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < server->max_clients; ++i) {
        if (server->connections[i].active) {
            ++count;
        }
    }
    return count;
}

} /* extern "C" */

/* Internal: handle CONNECTION_REQUEST packet */
namespace netudp {

void server_handle_connection_request(netudp_server* server,
    const netudp_address_t* from, const uint8_t* packet, int packet_len) {

    if (packet_len != 1078) {
        return;
    }

    /* Validate and decrypt token */
    const uint8_t* token_data = packet; /* The whole 1078-byte packet IS the request */
    /*
     * CONNECTION_REQUEST layout:
     *   prefix(1) + version(13) + protocol_id(8) + expire_ts(8) + nonce(24) + encrypted_private(1024)
     * We need to reconstruct a 2048-byte token for validation.
     * For now, validate directly from packet fields.
     */

    /* Check version */
    if (std::memcmp(token_data + 1, NETUDP_VERSION_INFO, NETUDP_VERSION_INFO_BYTES) != 0) {
        return;
    }

    /* Check protocol_id */
    uint64_t pkt_protocol_id = 0;
    std::memcpy(&pkt_protocol_id, token_data + 14, 8);
    if (pkt_protocol_id != server->protocol_id) {
        return;
    }

    /* Check expiry */
    uint64_t expire_ts = 0;
    std::memcpy(&expire_ts, token_data + 22, 8);
    uint64_t now_ts = static_cast<uint64_t>(server->current_time);
    if (now_ts >= expire_ts) {
        return;
    }

    /* Build AAD for decryption: version(13) + protocol_id(8) + expire_ts(8) */
    uint8_t aad[29];
    std::memcpy(aad, token_data + 1, 13);  /* version */
    std::memcpy(aad + 13, token_data + 14, 8); /* protocol_id */
    std::memcpy(aad + 21, token_data + 22, 8); /* expire_ts */

    /* Decrypt private data */
    const uint8_t* nonce = token_data + 30;
    const uint8_t* encrypted_private = token_data + 54;

    uint8_t decrypted[TOKEN_PRIVATE_SIZE];
    int dec_len = crypto::xchacha_decrypt(
        server->config.private_key, nonce, aad, 29,
        encrypted_private, TOKEN_PRIVATE_ENCRYPTED_SIZE, decrypted
    );
    if (dec_len < 0) {
        return;
    }

    /* Deserialize private token */
    PrivateConnectToken priv = {};
    if (deserialize_private_token(decrypted, TOKEN_PRIVATE_SIZE, &priv) != 0) {
        crypto_wipe(decrypted, sizeof(decrypted));
        return;
    }
    crypto_wipe(decrypted, sizeof(decrypted));

    /* Check if this server's address is in the token */
    /* (Skip for now — would need server's own address) */

    /* Check if client_id is already connected */
    for (int i = 0; i < server->max_clients; ++i) {
        if (server->connections[i].active && server->connections[i].client_id == priv.client_id) {
            return;
        }
    }

    /* Check token fingerprint */
    auto fp = compute_token_fingerprint(server->config.private_key,
                                         encrypted_private, TOKEN_PRIVATE_ENCRYPTED_SIZE);
    for (int i = 0; i < server->fingerprint_count; ++i) {
        auto& entry = server->fingerprint_cache[i];
        if (entry.used && std::memcmp(entry.fingerprint.hash, fp.hash, 8) == 0) {
            if (!netudp_address_equal(&entry.address, from)) {
                return; /* Same token from different IP — reject */
            }
            break;
        }
    }

    /* Record fingerprint */
    if (server->fingerprint_count < 1024) {
        auto& entry = server->fingerprint_cache[server->fingerprint_count++];
        entry.fingerprint = fp;
        entry.address = *from;
        entry.expire_time = expire_ts;
        entry.used = true;
    }

    /* Find free connection slot */
    int slot = -1;
    for (int i = 0; i < server->max_clients; ++i) {
        if (!server->connections[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return; /* Server full */
    }

    /* Establish connection directly (simplified — skip challenge/response for now) */
    Connection& conn = server->connections[slot];
    conn.active = true;
    conn.address = *from;
    conn.client_id = priv.client_id;
    std::memcpy(conn.user_data, priv.user_data, 256);
    std::memcpy(conn.key_epoch.tx_key, priv.server_to_client_key, 32);
    std::memcpy(conn.key_epoch.rx_key, priv.client_to_server_key, 32);
    conn.key_epoch.tx_nonce_counter = 0;
    conn.key_epoch.replay.reset();
    conn.connect_time = server->current_time;
    conn.last_recv_time = server->current_time;
    conn.timeout_seconds = priv.timeout_seconds;

    /* Fire callback */
    if (server->config.on_connect != nullptr) {
        server->config.on_connect(server->config.callback_context,
                                   slot, priv.client_id, priv.user_data);
    }

    /* Send KEEPALIVE to confirm connection (prefix 0x05, with client_index + max_clients) */
    uint8_t keepalive[16] = {};
    keepalive[0] = 0x15; /* KEEPALIVE, 1-byte seq */

    uint32_t client_idx = static_cast<uint32_t>(slot);
    uint32_t max_cl = static_cast<uint32_t>(server->max_clients);

    uint8_t payload[8];
    std::memcpy(payload, &client_idx, 4);
    std::memcpy(payload + 4, &max_cl, 4);

    uint8_t ct[64];
    int ct_len = crypto::packet_encrypt(&conn.key_epoch, server->protocol_id, keepalive[0],
                                         payload, 8, ct);
    if (ct_len > 0) {
        std::memcpy(keepalive + 1, ct, static_cast<size_t>(ct_len));
        socket_send(&server->socket, from, keepalive, 1 + ct_len);
    }
}

} // namespace netudp
