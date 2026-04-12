#include <netudp/netudp.h>
#include <netudp/netudp_token.h>
#include <netudp/netudp_buffer.h>
#include "connection/connection.h"
#include "connection/connect_token.h"
#include "connection/rate_limiter.h"
#include "connection/ddos.h"
#include "socket/socket.h"
#include "crypto/packet_crypto.h"
#include "crypto/random.h"
#include "crypto/xchacha.h"
#include "crypto/vendor/monocypher.h"
#include "core/address.h"
#include "core/allocator.h"
#include "wire/frame.h"
#include "reliability/packet_tracker.h"

#include <cstring>
#include <ctime>
#include <new>
#include <algorithm>

/* ======================================================================
 * Server struct — global scope to match forward decl in netudp_types.h
 * ====================================================================== */

struct netudp_server {
    bool running = false;
    int  max_clients = 0;

    netudp_server_config_t config = {};
    uint64_t protocol_id = 0;

    netudp::Socket socket;
    netudp::Allocator allocator;
    netudp::RateLimiter rate_limiter;
    netudp::DDoSMonitor ddos;

    netudp::Connection* connections = nullptr;

    uint8_t challenge_key[32] = {};
    uint64_t challenge_sequence = 0;

    struct FingerprintEntry {
        netudp::TokenFingerprint fingerprint;
        netudp_address_t address;
        uint64_t expire_time;
        bool used;
    };
    FingerprintEntry fingerprint_cache[1024] = {};
    int fingerprint_count = 0;

    double current_time = 0.0;
    double last_time = 0.0;
    double last_cleanup_time = 0.0;

    uint8_t recv_buf[NETUDP_MAX_PACKET_ON_WIRE] = {};
    uint8_t send_buf[NETUDP_MAX_PACKET_ON_WIRE] = {};
};

/* Forward declarations for internal functions */
namespace netudp {
void server_handle_connection_request(netudp_server* server,
    const netudp_address_t* from, const uint8_t* packet, int packet_len);
void server_handle_data_packet(netudp_server* server, int slot,
    const uint8_t* packet, int packet_len);
void server_send_pending(netudp_server* server, int slot);
void server_send_keepalive(netudp_server* server, int slot);
}

/* ======================================================================
 * Extern "C" API
 * ====================================================================== */

extern "C" {

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
    server->last_time = time;

    server->allocator.context = config->allocator_context;
    server->allocator.alloc = config->allocate_function;
    server->allocator.free = config->free_function;

    netudp_address_t bind_addr = {};
    if (netudp_parse_address(address, &bind_addr) != NETUDP_OK) {
        delete server;
        return nullptr;
    }

    if (netudp::socket_create(&server->socket, &bind_addr, 4 * 1024 * 1024, 4 * 1024 * 1024) != NETUDP_OK) {
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

    server->connections = static_cast<netudp::Connection*>(
        server->allocator.allocate(sizeof(netudp::Connection) * static_cast<size_t>(max_clients)));
    if (server->connections != nullptr) {
        for (int i = 0; i < max_clients; ++i) {
            new (&server->connections[i]) netudp::Connection();
        }
    }

    netudp::crypto::random_bytes(server->challenge_key, 32);
    server->challenge_sequence = 0;
}

void netudp_server_stop(netudp_server_t* server) {
    if (server == nullptr) {
        return;
    }
    server->running = false;

    if (server->connections != nullptr) {
        for (int i = 0; i < server->max_clients; ++i) {
            server->connections[i].reset();
        }
        server->allocator.deallocate(server->connections);
        server->connections = nullptr;
    }
}

void netudp_server_update(netudp_server_t* server, double time) {
    if (server == nullptr || !server->running) {
        return;
    }

    double dt = time - server->current_time;
    server->current_time = time;

    /* DDoS monitor tick */
    server->ddos.update(dt);

    /* Receive packets */
    for (int recv_iter = 0; recv_iter < 1024; ++recv_iter) {
        netudp_address_t from = {};
        int received = netudp::socket_recv(&server->socket, &from,
                                            server->recv_buf, sizeof(server->recv_buf));
        if (received <= 0) {
            break;
        }

        /* Rate limit */
        if (!server->rate_limiter.allow(&from, time)) {
            server->ddos.on_bad_packet();
            continue;
        }

        uint8_t prefix = server->recv_buf[0];
        uint8_t packet_type = prefix & 0x0F;

        if (packet_type == 0x00 && received == 1078) {
            /* CONNECTION_REQUEST */
            if (!server->ddos.should_process_new_connection()) {
                continue;
            }
            netudp::server_handle_connection_request(server, &from, server->recv_buf, received);
        } else if (packet_type >= 0x04 && packet_type <= 0x06) {
            /* DATA / KEEPALIVE / DISCONNECT — find connection by address */
            int slot = -1;
            for (int i = 0; i < server->max_clients; ++i) {
                if (server->connections[i].active &&
                    netudp_address_equal(&server->connections[i].address, &from)) {
                    slot = i;
                    break;
                }
            }
            if (slot >= 0) {
                netudp::server_handle_data_packet(server, slot, server->recv_buf, received);
            } else {
                server->ddos.on_bad_packet();
            }
        } else {
            server->ddos.on_bad_packet();
        }
    }

    /* Per-connection: send pending, keepalive, timeout, stats */
    for (int i = 0; i < server->max_clients; ++i) {
        netudp::Connection& conn = server->connections[i];
        if (!conn.active) {
            continue;
        }

        /* Bandwidth refill */
        conn.bandwidth.refill(time);
        conn.budget.refill(dt, conn.congestion.send_rate());

        /* Send pending channel data */
        netudp::server_send_pending(server, i);

        /* Keepalive */
        if (time - conn.last_send_time > 1.0) {
            netudp::server_send_keepalive(server, i);
        }

        /* Timeout */
        if (time - conn.last_recv_time > conn.timeout_seconds) {
            if (server->config.on_disconnect != nullptr) {
                server->config.on_disconnect(server->config.callback_context, i, -4);
            }
            conn.reset();
            continue;
        }

        /* Stats */
        conn.stats.update_throughput(time);
        conn.stats.ping_ms = conn.rtt.ping_ms();
        conn.stats.send_rate_bytes_per_sec = conn.congestion.send_rate();
        conn.stats.max_send_rate_bytes_per_sec = conn.congestion.max_send_rate();

        /* Fragment timeout cleanup */
        conn.fragment_reassembler.cleanup_timeout(time);

        /* Congestion evaluation (every ~RTT) */
        conn.congestion.evaluate();
    }

    /* Rate limiter cleanup */
    if (time - server->last_cleanup_time > 1.0) {
        server->rate_limiter.cleanup(time);
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
    netudp::socket_destroy(&server->socket);
    crypto_wipe(server->challenge_key, sizeof(server->challenge_key));
    delete server;
}

/* ======================================================================
 * Send — full pipeline: channel → fragment → multiframe → encrypt → socket
 * ====================================================================== */

int netudp_server_send(netudp_server_t* server, int client_index,
                       int channel, const void* data, int bytes, int flags) {
    if (server == nullptr || !server->running) {
        return NETUDP_ERROR_NOT_INITIALIZED;
    }
    if (client_index < 0 || client_index >= server->max_clients) {
        return NETUDP_ERROR_INVALID_PARAM;
    }
    netudp::Connection& conn = server->connections[client_index];
    if (!conn.active) {
        return NETUDP_ERROR_NOT_CONNECTED;
    }
    if (channel < 0 || channel >= conn.num_channels) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    /* Queue into channel */
    if (!conn.channels[channel].queue_send(static_cast<const uint8_t*>(data), bytes, flags)) {
        return NETUDP_ERROR_NO_BUFFERS;
    }
    conn.channels[channel].start_nagle(server->current_time);

    /* If NO_DELAY, flush immediately */
    if ((flags & NETUDP_SEND_NO_DELAY) != 0) {
        conn.channels[channel].flush();
        netudp::server_send_pending(server, client_index);
    }

    return NETUDP_OK;
}

/* ======================================================================
 * Receive — deliver messages from connection's delivered_messages queue
 * ====================================================================== */

int netudp_server_receive(netudp_server_t* server, int client_index,
                          netudp_message_t** messages, int max_messages) {
    if (server == nullptr || !server->running || messages == nullptr) {
        return 0;
    }
    if (client_index < 0 || client_index >= server->max_clients) {
        return 0;
    }
    netudp::Connection& conn = server->connections[client_index];
    if (!conn.active) {
        return 0;
    }

    int count = 0;
    while (count < max_messages && !conn.delivered_messages.is_empty()) {
        netudp::DeliveredMessage dmsg;
        if (!conn.delivered_messages.pop_front(&dmsg)) {
            break;
        }
        if (!dmsg.valid) {
            continue;
        }

        /* Allocate a message struct for the app */
        auto* msg = static_cast<netudp_message_t*>(std::malloc(sizeof(netudp_message_t)));
        if (msg == nullptr) {
            break;
        }
        msg->data = std::malloc(static_cast<size_t>(dmsg.size));
        if (msg->data == nullptr) {
            std::free(msg);
            break;
        }
        std::memcpy(msg->data, dmsg.data, static_cast<size_t>(dmsg.size));
        msg->size = dmsg.size;
        msg->channel = dmsg.channel;
        msg->client_index = client_index;
        msg->flags = 0;
        msg->message_number = dmsg.sequence;
        msg->receive_time_us = 0;

        messages[count++] = msg;
    }

    return count;
}

void netudp_message_release(netudp_message_t* message) {
    if (message == nullptr) {
        return;
    }
    std::free(message->data);
    std::free(message);
}

/* ======================================================================
 * Broadcast / Flush
 * ====================================================================== */

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

void netudp_server_flush(netudp_server_t* server, int client_index) {
    if (server == nullptr || !server->running) {
        return;
    }
    if (client_index < 0 || client_index >= server->max_clients) {
        return;
    }
    netudp::Connection& conn = server->connections[client_index];
    if (!conn.active) {
        return;
    }
    for (int ch = 0; ch < conn.num_channels; ++ch) {
        conn.channels[ch].flush();
    }
    netudp::server_send_pending(server, client_index);
}

} /* extern "C" */

/* ======================================================================
 * Internal: Connection request handling
 * ====================================================================== */

namespace netudp {

void server_handle_connection_request(netudp_server* server,
    const netudp_address_t* from, const uint8_t* packet, int packet_len) {
    if (packet_len != 1078) {
        return;
    }

    const uint8_t* token_data = packet;

    if (std::memcmp(token_data + 1, NETUDP_VERSION_INFO, NETUDP_VERSION_INFO_BYTES) != 0) {
        return;
    }

    uint64_t pkt_protocol_id = 0;
    std::memcpy(&pkt_protocol_id, token_data + 14, 8);
    if (pkt_protocol_id != server->protocol_id) {
        return;
    }

    uint64_t expire_ts = 0;
    std::memcpy(&expire_ts, token_data + 22, 8);
    uint64_t now_ts = static_cast<uint64_t>(server->current_time);
    if (now_ts >= expire_ts) {
        return;
    }

    uint8_t aad[29];
    std::memcpy(aad, token_data + 1, 13);
    std::memcpy(aad + 13, token_data + 14, 8);
    std::memcpy(aad + 21, token_data + 22, 8);

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

    PrivateConnectToken priv = {};
    if (deserialize_private_token(decrypted, TOKEN_PRIVATE_SIZE, &priv) != 0) {
        crypto_wipe(decrypted, sizeof(decrypted));
        return;
    }
    crypto_wipe(decrypted, sizeof(decrypted));

    for (int i = 0; i < server->max_clients; ++i) {
        if (server->connections[i].active && server->connections[i].client_id == priv.client_id) {
            return;
        }
    }

    auto fp = compute_token_fingerprint(server->config.private_key,
                                         encrypted_private, TOKEN_PRIVATE_ENCRYPTED_SIZE);
    for (int i = 0; i < server->fingerprint_count; ++i) {
        auto& entry = server->fingerprint_cache[i];
        if (entry.used && std::memcmp(entry.fingerprint.hash, fp.hash, 8) == 0) {
            if (!netudp_address_equal(&entry.address, from)) {
                return;
            }
            break;
        }
    }

    if (server->fingerprint_count < 1024) {
        auto& entry = server->fingerprint_cache[server->fingerprint_count++];
        entry.fingerprint = fp;
        entry.address = *from;
        entry.expire_time = expire_ts;
        entry.used = true;
    }

    int slot = -1;
    for (int i = 0; i < server->max_clients; ++i) {
        if (!server->connections[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;
    }

    /* Establish connection with full subsystem init */
    Connection& conn = server->connections[slot];
    conn.active = true;
    conn.address = *from;
    conn.client_id = priv.client_id;
    std::memcpy(conn.user_data, priv.user_data, 256);
    std::memcpy(conn.key_epoch.tx_key, priv.server_to_client_key, 32);
    std::memcpy(conn.key_epoch.rx_key, priv.client_to_server_key, 32);
    conn.key_epoch.tx_nonce_counter = 0;
    conn.key_epoch.replay.reset();
    conn.timeout_seconds = priv.timeout_seconds;

    /* Init ALL subsystems */
    conn.init_subsystems(server->config.channels, server->config.num_channels, server->current_time);

    if (server->config.on_connect != nullptr) {
        server->config.on_connect(server->config.callback_context,
                                   slot, priv.client_id, priv.user_data);
    }

    /* Send KEEPALIVE */
    server_send_keepalive(server, slot);
}

/* ======================================================================
 * Internal: Handle data packet from established connection
 * ====================================================================== */

void server_handle_data_packet(netudp_server* server, int slot,
    const uint8_t* packet, int packet_len) {
    Connection& conn = server->connections[slot];
    if (packet_len < 2) {
        return;
    }

    uint8_t prefix = packet[0];

    /* Decrypt: everything after prefix byte */
    int header_len = 1;

    /* Nonce counter: next expected from this connection's replay window */
    uint64_t expected_nonce = conn.key_epoch.replay.most_recent + 1;

    uint8_t plaintext[NETUDP_MAX_PACKET_ON_WIRE];
    int pt_len = crypto::packet_decrypt(
        &conn.key_epoch, server->protocol_id, prefix,
        expected_nonce,
        packet + header_len, packet_len - header_len, plaintext
    );

    if (pt_len < 0) {
        conn.stats.decrypt_failures++;
        server->ddos.on_bad_packet();
        return;
    }

    conn.last_recv_time = server->current_time;
    conn.stats.on_packet_received(packet_len);

    /* Parse AckFields (first 8 bytes of plaintext) */
    if (pt_len < 8) {
        return;
    }

    AckFields ack_fields = read_ack_fields(plaintext);

    /* Process acks — determine which of our sent packets were received */
    int newly_acked = conn.packet_tracker.process_acks(ack_fields);
    for (int i = 0; i < newly_acked; ++i) {
        conn.congestion.on_packet_acked();
    }

    /* RTT from ack */
    double send_time = conn.packet_tracker.get_send_time(ack_fields.ack);
    if (send_time > 0.0) {
        conn.rtt.on_sample(send_time, server->current_time, ack_fields.ack_delay_us);
        conn.congestion.on_rtt_sample();
    }

    /* Record that we received this packet (for our ack generation) */
    uint16_t pkt_seq = 0; /* Would come from wire header — simplified for now */
    conn.packet_tracker.on_packet_received(pkt_seq, server->current_time);

    /* Parse frames after AckFields */
    int pos = 8;
    while (pos < pt_len) {
        uint8_t frame_type = plaintext[pos];
        pos++;

        if (frame_type == wire::FRAME_UNRELIABLE_DATA && pos + 3 <= pt_len) {
            uint8_t ch = plaintext[pos++];
            uint16_t msg_len = 0;
            std::memcpy(&msg_len, plaintext + pos, 2);
            pos += 2;
            if (pos + msg_len > pt_len || ch >= conn.num_channels) {
                break;
            }
            conn.stats.messages_received++;
            /* Deliver unreliable message */
            DeliveredMessage dmsg;
            std::memcpy(dmsg.data, plaintext + pos, msg_len);
            dmsg.size = msg_len;
            dmsg.channel = ch;
            dmsg.sequence = 0;
            dmsg.valid = true;
            conn.delivered_messages.push_back(dmsg);
            pos += msg_len;

        } else if (frame_type == wire::FRAME_RELIABLE_DATA && pos + 5 <= pt_len) {
            uint8_t ch = plaintext[pos++];
            uint16_t msg_seq = 0;
            std::memcpy(&msg_seq, plaintext + pos, 2);
            pos += 2;
            uint16_t msg_len = 0;
            std::memcpy(&msg_len, plaintext + pos, 2);
            pos += 2;
            if (pos + msg_len > pt_len || ch >= conn.num_channels) {
                break;
            }

            uint8_t ch_type = conn.channels[ch].type();
            if (ch_type == NETUDP_CHANNEL_RELIABLE_ORDERED) {
                if (conn.reliable_state[ch].buffer_received_ordered(msg_seq, plaintext + pos, msg_len)) {
                    conn.reliable_state[ch].deliver_ordered(
                        [&](const uint8_t* data, int len, uint16_t seq) {
                            DeliveredMessage dmsg;
                            int copy_len = std::min(len, static_cast<int>(NETUDP_MTU));
                            std::memcpy(dmsg.data, data, static_cast<size_t>(copy_len));
                            dmsg.size = copy_len;
                            dmsg.channel = ch;
                            dmsg.sequence = seq;
                            dmsg.valid = true;
                            conn.delivered_messages.push_back(dmsg);
                            conn.stats.messages_received++;
                        });
                }
            } else if (ch_type == NETUDP_CHANNEL_RELIABLE_UNORDERED) {
                if (!conn.reliable_state[ch].is_received_unordered(msg_seq)) {
                    conn.reliable_state[ch].mark_received_unordered(msg_seq);
                    DeliveredMessage dmsg;
                    int copy_len = std::min(static_cast<int>(msg_len), static_cast<int>(NETUDP_MTU));
                    std::memcpy(dmsg.data, plaintext + pos, static_cast<size_t>(copy_len));
                    dmsg.size = copy_len;
                    dmsg.channel = ch;
                    dmsg.sequence = msg_seq;
                    dmsg.valid = true;
                    conn.delivered_messages.push_back(dmsg);
                    conn.stats.messages_received++;
                }
            }
            pos += msg_len;

        } else if (frame_type == wire::FRAME_FRAGMENT_DATA && pos + 5 <= pt_len) {
            uint8_t ch = plaintext[pos++];
            uint16_t msg_id = 0;
            std::memcpy(&msg_id, plaintext + pos, 2);
            pos += 2;
            uint8_t frag_idx = plaintext[pos++];
            uint8_t frag_cnt = plaintext[pos++];
            int frag_len = pt_len - pos; /* Rest is fragment data */
            if (frag_len <= 0) {
                break;
            }

            int out_size = 0;
            const uint8_t* complete = conn.fragment_reassembler.on_fragment_received(
                msg_id, frag_idx, frag_cnt, plaintext + pos, frag_len,
                NETUDP_MTU - 64, server->current_time, &out_size
            );
            if (complete != nullptr && out_size > 0) {
                DeliveredMessage dmsg;
                int copy_len = std::min(out_size, static_cast<int>(NETUDP_MTU));
                std::memcpy(dmsg.data, complete, static_cast<size_t>(copy_len));
                dmsg.size = copy_len;
                dmsg.channel = ch;
                dmsg.valid = true;
                conn.delivered_messages.push_back(dmsg);
                conn.stats.fragments_received++;
            }
            conn.stats.fragments_received++;
            pos = pt_len; /* Fragment consumes rest */

        } else if (frame_type == wire::FRAME_DISCONNECT) {
            if (server->config.on_disconnect != nullptr) {
                int reason = (pos < pt_len) ? plaintext[pos] : 0;
                server->config.on_disconnect(server->config.callback_context, slot, reason);
            }
            conn.reset();
            return;
        } else {
            break; /* Unknown frame type */
        }
    }
}

/* ======================================================================
 * Internal: Send pending channel data
 * ====================================================================== */

void server_send_pending(netudp_server* server, int slot) {
    Connection& conn = server->connections[slot];
    double now = server->current_time;

    /* Check bandwidth */
    if (!conn.budget.can_send()) {
        return;
    }

    /* Find next channel with pending data */
    int ch_idx = ChannelScheduler::next_channel(conn.channels, conn.num_channels, now);
    while (ch_idx >= 0) {
        QueuedMessage qmsg;
        if (!conn.channels[ch_idx].dequeue_send(&qmsg)) {
            break;
        }

        /* Build packet: prefix + encrypted(AckFields + frame) */
        uint8_t payload[NETUDP_MTU];
        int payload_pos = 0;

        /* AckFields */
        AckFields ack = conn.packet_tracker.build_ack_fields(now);
        payload_pos += write_ack_fields(ack, payload + payload_pos);

        /* Frame */
        uint8_t ch_type = conn.channels[ch_idx].type();
        int frame_len = 0;

        if (ch_type == NETUDP_CHANNEL_RELIABLE_ORDERED || ch_type == NETUDP_CHANNEL_RELIABLE_UNORDERED) {
            uint16_t pkt_seq = conn.packet_tracker.send_sequence();
            conn.reliable_state[ch_idx].record_send(qmsg.data, qmsg.size, pkt_seq, now);
            frame_len = wire::write_reliable_frame(
                payload + payload_pos, NETUDP_MTU - payload_pos,
                static_cast<uint8_t>(ch_idx), qmsg.sequence, qmsg.data, qmsg.size);
        } else {
            frame_len = wire::write_unreliable_frame(
                payload + payload_pos, NETUDP_MTU - payload_pos,
                static_cast<uint8_t>(ch_idx), qmsg.data, qmsg.size);
        }

        if (frame_len < 0) {
            break;
        }
        payload_pos += frame_len;

        /* Record packet send (return value is the sequence, used for ack matching) */
        conn.packet_tracker.send_packet(now);

        /* Encrypt */
        uint8_t prefix = 0x14; /* DATA, 1-byte seq */
        uint8_t ct[NETUDP_MAX_PACKET_ON_WIRE];
        int ct_len = crypto::packet_encrypt(&conn.key_epoch, server->protocol_id, prefix,
                                             payload, payload_pos, ct);
        if (ct_len < 0) {
            break;
        }

        /* Assemble wire packet */
        server->send_buf[0] = prefix;
        std::memcpy(server->send_buf + 1, ct, static_cast<size_t>(ct_len));
        int total = 1 + ct_len;

        /* Send */
        socket_send(&server->socket, &conn.address, server->send_buf, total);
        conn.last_send_time = now;
        conn.stats.on_packet_sent(total);
        conn.budget.consume(total);

        /* Next channel */
        ch_idx = ChannelScheduler::next_channel(conn.channels, conn.num_channels, now);

        if (!conn.budget.can_send()) {
            break;
        }
    }
}

/* ======================================================================
 * Internal: Send keepalive packet
 * ====================================================================== */

void server_send_keepalive(netudp_server* server, int slot) {
    Connection& conn = server->connections[slot];
    double now = server->current_time;

    /* Keepalive: prefix + encrypted(AckFields only) */
    uint8_t payload[8];
    AckFields ack = conn.packet_tracker.build_ack_fields(now);
    write_ack_fields(ack, payload);

    conn.packet_tracker.send_packet(now);

    uint8_t prefix = 0x15; /* KEEPALIVE */
    uint8_t ct[64];
    int ct_len = crypto::packet_encrypt(&conn.key_epoch, server->protocol_id, prefix,
                                         payload, 8, ct);
    if (ct_len <= 0) {
        return;
    }

    server->send_buf[0] = prefix;
    std::memcpy(server->send_buf + 1, ct, static_cast<size_t>(ct_len));
    socket_send(&server->socket, &conn.address, server->send_buf, 1 + ct_len);
    conn.last_send_time = now;
    conn.last_keepalive_time = now;
}

} // namespace netudp
