#include <netudp/netudp.h>
#include <netudp/netudp_token.h>
#include "connection/connection.h"
#include "connection/connect_token.h"
#include "connection/client_state.h"
#include "socket/socket.h"
#include "crypto/packet_crypto.h"
#include "crypto/random.h"
#include "crypto/vendor/monocypher.h"
#include "core/address.h"
#include "core/log.h"
#include "wire/frame.h"
#include "profiling/profiler.h"
#include "reliability/packet_tracker.h"

#include <cstring>
#include <new>
#include <algorithm>

/* ======================================================================
 * Client struct — global scope to match forward decl in netudp_types.h
 * ====================================================================== */

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

    /* Full connection state with all subsystems */
    netudp::Connection conn;

    int     client_index = -1;
    int     max_clients = 0;

    double  connect_start_time = 0.0;
    double  last_send_time = 0.0;
    double  current_time = 0.0;

    uint8_t recv_buf[NETUDP_MAX_PACKET_ON_WIRE] = {};
    uint8_t send_buf[NETUDP_MAX_PACKET_ON_WIRE] = {};
};

/* Internal functions */
namespace netudp {
void client_send_pending(netudp_client* client);
void client_handle_data_packet(netudp_client* client, const uint8_t* packet, int packet_len);
}

/* ======================================================================
 * Extern "C" API
 * ====================================================================== */

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

    /* Setup connection with all subsystems */
    std::memcpy(client->conn.key_epoch.tx_key, client->client_to_server_key, 32);
    std::memcpy(client->conn.key_epoch.rx_key, client->server_to_client_key, 32);
    client->conn.key_epoch.tx_nonce_counter = 0;
    client->conn.key_epoch.replay.reset();
    client->conn.init_subsystems(client->config.channels, client->config.num_channels, client->current_time);

    client->current_server_index = 0;
    client->state = netudp::ClientState::SENDING_REQUEST;
    client->connect_start_time = client->current_time;
    client->last_send_time = 0.0;

    /* Connect UDP socket to server address — caches route, saves 1-3us per send */
    const netudp_address_t* dest = &client->server_addresses[client->current_server_index];
    netudp::socket_connect(&client->socket, dest);
}

void netudp_client_update(netudp_client_t* client, double time) {
    NETUDP_ZONE("cli::update");
    if (client == nullptr) {
        return;
    }
    double dt = time - client->current_time;
    client->current_time = time;

    if (client->state == netudp::ClientState::SENDING_REQUEST) {
        if (time - client->last_send_time > 0.1) {
            /*
             * CONNECTION_REQUEST (1078 bytes):
             *   prefix(1)=0x00 + version(13) + protocol_id(8) + expire_ts(8)
             *   + nonce(24) + encrypted_private(1024)
             * Note: create_ts (token offset 21, 8 bytes) is NOT sent on wire.
             */
            uint8_t request[1078];
            int rpos = 0;
            request[rpos++] = 0x00;
            std::memcpy(request + rpos, client->connect_token + 0, 13);  /* version */
            rpos += 13;
            std::memcpy(request + rpos, client->connect_token + 13, 8);  /* protocol_id */
            rpos += 8;
            /* Skip create_ts at token offset 21 (8 bytes) */
            std::memcpy(request + rpos, client->connect_token + 29, 8);  /* expire_ts */
            rpos += 8;
            std::memcpy(request + rpos, client->connect_token + 37, 24); /* nonce */
            rpos += 24;
            int priv_bytes = 1078 - rpos; /* = 1024 */
            std::memcpy(request + rpos, client->connect_token + 61,
                        static_cast<size_t>(priv_bytes));

            netudp::socket_send_connected(&client->socket, request, 1078);
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
        /* Bandwidth refill */
        client->conn.bandwidth.refill(time);
        client->conn.budget.refill(dt, client->conn.congestion.send_rate());

        /* Send pending channel data */
        netudp::client_send_pending(client);

        /* Keepalive */
        if (time - client->conn.last_send_time > 1.0) {
            uint8_t payload[8];
            netudp::AckFields ack = client->conn.packet_tracker.build_ack_fields(time);
            netudp::write_ack_fields(ack, payload);
            client->conn.packet_tracker.send_packet(time);

            uint8_t prefix = 0x15;
            uint8_t ct[64];
            int ct_len = netudp::crypto::packet_encrypt(
                &client->conn.key_epoch, client->protocol_id, prefix, payload, 8, ct);
            if (ct_len > 0) {
                client->send_buf[0] = prefix;
                std::memcpy(client->send_buf + 1, ct, static_cast<size_t>(ct_len));
                netudp::socket_send_connected(&client->socket, client->send_buf, 1 + ct_len);
                client->conn.last_send_time = time;
            }
        }

        /* Timeout */
        if (time - client->conn.last_recv_time > client->timeout_seconds) {
            NLOG_WARN("[netudp] client connection timed out (idle=%.1fs)",
                            time - client->conn.last_recv_time);
            client->state = netudp::ClientState::CONNECTION_TIMED_OUT;
            return;
        }

        /* Stats + congestion */
        client->conn.stats.update_throughput(time);
        client->conn.congestion.evaluate();
        if (client->conn.cdata != nullptr) client->conn.frag().cleanup_timeout(time);
    }

    /* Receive packets */
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
            /* KEEPALIVE — connection accepted */
            int header_len = 1;
            uint8_t payload[64] = {};
            int pt_len = netudp::crypto::packet_decrypt(
                &client->conn.key_epoch, client->protocol_id, prefix,
                0, client->recv_buf + header_len, received - header_len, payload);

            if (pt_len >= 8) {
                std::memcpy(&client->client_index, payload, 4);
                std::memcpy(&client->max_clients, payload + 4, 4);
                client->state = netudp::ClientState::CONNECTED;
                client->conn.last_recv_time = time;
                client->conn.active = true;
                NLOG_INFO("[netudp] client connected (slot=%d, max_clients=%d)",
                                client->client_index, client->max_clients);
            }
        } else if (packet_type >= 0x04 && client->state == netudp::ClientState::CONNECTED) {
            netudp::client_handle_data_packet(client, client->recv_buf, received);
        }
    }
}

void netudp_client_disconnect(netudp_client_t* client) {
    if (client == nullptr) {
        return;
    }
    NLOG_INFO("[netudp] client disconnect requested (slot=%d)", client->client_index);
    client->state = netudp::ClientState::DISCONNECTED;
    client->client_index = -1;
    client->conn.active = false;
}

void netudp_client_destroy(netudp_client_t* client) {
    if (client == nullptr) {
        return;
    }
    netudp::socket_destroy(&client->socket);
    crypto_wipe(client->client_to_server_key, 32);
    crypto_wipe(client->server_to_client_key, 32);
    client->conn.reset();
    delete client;
}

int netudp_client_state(const netudp_client_t* client) {
    if (client == nullptr) {
        return 0;
    }
    return static_cast<int>(client->state);
}

int netudp_client_send(netudp_client_t* client,
                       int channel, const void* data, int bytes, int flags) {
    NETUDP_ZONE("cli::send");
    if (client == nullptr || client->state != netudp::ClientState::CONNECTED) {
        return NETUDP_ERROR_NOT_CONNECTED;
    }
    if (channel < 0 || channel >= client->conn.num_channels) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    if (!client->conn.ch(channel).queue_send(
            static_cast<const uint8_t*>(data), bytes, flags)) {
        return NETUDP_ERROR_NO_BUFFERS;
    }
    client->conn.ch(channel).start_nagle(client->current_time);

    if ((flags & NETUDP_SEND_NO_DELAY) != 0) {
        client->conn.ch(channel).flush();
        netudp::client_send_pending(client);
    }

    return NETUDP_OK;
}

int netudp_client_receive(netudp_client_t* client,
                          netudp_message_t** messages, int max_messages) {
    if (client == nullptr || messages == nullptr ||
        client->state != netudp::ClientState::CONNECTED) {
        return 0;
    }

    int count = 0;
    while (count < max_messages && !client->conn.delivered().is_empty()) {
        netudp::DeliveredMessage dmsg;
        if (!client->conn.delivered().pop_front(&dmsg)) {
            break;
        }
        if (!dmsg.valid) {
            continue;
        }

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
        msg->client_index = client->client_index;
        msg->flags = 0;
        msg->message_number = dmsg.sequence;
        msg->receive_time_us = 0;

        messages[count++] = msg;
    }

    return count;
}

void netudp_client_flush(netudp_client_t* client) {
    if (client == nullptr || client->state != netudp::ClientState::CONNECTED) {
        return;
    }
    for (int ch = 0; ch < client->conn.num_channels; ++ch) {
        client->conn.ch(ch).flush();
    }
    netudp::client_send_pending(client);
}

} /* extern "C" */

/* ======================================================================
 * Internal: Send pending data from channels
 * ====================================================================== */

namespace netudp {

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — coalescing loop with multiple frame types
void client_send_pending(netudp_client* client) {
    NETUDP_ZONE("cli::send_pending");
    Connection& conn = client->conn;
    double now = client->current_time;

    if (!conn.budget.can_send()) {
        return;
    }

    static constexpr int kPayloadBudget = NETUDP_MTU;

    uint8_t payload[NETUDP_MTU];
    int payload_pos = 0;
    int frames_packed = 0;

    AckFields ack = conn.packet_tracker.build_ack_fields(now);
    payload_pos += write_ack_fields(ack, payload + payload_pos);

    int ch_idx = ChannelScheduler::next_channel(conn.cdata->channels, conn.num_channels, now);
    while (ch_idx >= 0) {
        NETUDP_ZONE("cli::coalesce");

        QueuedMessage qmsg;
        if (!conn.ch(ch_idx).dequeue_send(&qmsg)) {
            ch_idx = ChannelScheduler::next_channel(conn.cdata->channels, conn.num_channels, now);
            continue;
        }

        uint8_t ch_type = conn.ch(ch_idx).type();
        bool is_reliable = (ch_type == NETUDP_CHANNEL_RELIABLE_ORDERED ||
                            ch_type == NETUDP_CHANNEL_RELIABLE_UNORDERED);
        int frame_overhead = is_reliable ? 6 : 4;
        int needed = frame_overhead + qmsg.size;
        int remaining = kPayloadBudget - payload_pos;

        /* Flush if this frame doesn't fit and we already have frames */
        if (needed > remaining && frames_packed > 0) {
            conn.packet_tracker.send_packet(now);

            uint8_t prefix = 0x14;
            uint8_t ct[NETUDP_MAX_PACKET_ON_WIRE];
            int ct_len = crypto::packet_encrypt(&conn.key_epoch, client->protocol_id, prefix,
                                                 payload, payload_pos, ct);
            if (ct_len < 0) {
                NLOG_ERROR("[netudp] cli::send_pending: encryption failed");
                break;
            }

            client->send_buf[0] = prefix;
            std::memcpy(client->send_buf + 1, ct, static_cast<size_t>(ct_len));
            int total = 1 + ct_len;
            socket_send_connected(&client->socket, client->send_buf, total);
            conn.last_send_time = now;
            conn.stats.on_packet_sent(total);
            conn.stats.frames_coalesced += static_cast<uint32_t>(frames_packed);
            conn.budget.consume(total);

            if (!conn.budget.can_send()) {
                return;
            }

            payload_pos = 0;
            ack = conn.packet_tracker.build_ack_fields(now);
            payload_pos += write_ack_fields(ack, payload + payload_pos);
            frames_packed = 0;
            remaining = kPayloadBudget - payload_pos;
        }

        int frame_len = 0;
        if (is_reliable) {
            uint16_t pkt_seq = conn.packet_tracker.send_sequence();
            conn.rs(ch_idx).record_send(qmsg.data, qmsg.size, pkt_seq, now);
            frame_len = wire::write_reliable_frame(
                payload + payload_pos, remaining,
                static_cast<uint8_t>(ch_idx), qmsg.sequence, qmsg.data, qmsg.size);
        } else {
            frame_len = wire::write_unreliable_frame(
                payload + payload_pos, remaining,
                static_cast<uint8_t>(ch_idx), qmsg.data, qmsg.size);
        }

        if (frame_len < 0) {
            NLOG_ERROR("[netudp] cli::send_pending: frame encode failed (ch=%d)", ch_idx);
            break;
        }

        payload_pos += frame_len;
        frames_packed++;

        ch_idx = ChannelScheduler::next_channel(conn.cdata->channels, conn.num_channels, now);
    }

    /* Flush remaining frames */
    if (frames_packed > 0) {
        conn.packet_tracker.send_packet(now);

        uint8_t prefix = 0x14;
        uint8_t ct[NETUDP_MAX_PACKET_ON_WIRE];
        int ct_len = crypto::packet_encrypt(&conn.key_epoch, client->protocol_id, prefix,
                                             payload, payload_pos, ct);
        if (ct_len >= 0) {
            client->send_buf[0] = prefix;
            std::memcpy(client->send_buf + 1, ct, static_cast<size_t>(ct_len));
            int total = 1 + ct_len;
            socket_send_connected(&client->socket, client->send_buf, total);
            conn.last_send_time = now;
            conn.stats.on_packet_sent(total);
            conn.stats.frames_coalesced += static_cast<uint32_t>(frames_packed);
            conn.budget.consume(total);
        } else {
            NLOG_ERROR("[netudp] cli::send_pending: final encryption failed");
        }
    }
}

/* ======================================================================
 * Internal: Handle data packet (same logic as server side)
 * ====================================================================== */

void client_handle_data_packet(netudp_client* client, const uint8_t* packet, int packet_len) {
    NETUDP_ZONE("cli::data_packet");
    Connection& conn = client->conn;
    if (packet_len < 2) {
        return;
    }

    uint8_t prefix = packet[0];
    int header_len = 1;

    uint64_t expected_nonce = conn.key_epoch.replay.most_recent + 1;

    uint8_t plaintext[NETUDP_MAX_PACKET_ON_WIRE];
    int pt_len = crypto::packet_decrypt(
        &conn.key_epoch, client->protocol_id, prefix,
        expected_nonce, packet + header_len, packet_len - header_len, plaintext);

    if (pt_len < 0) {
        conn.stats.decrypt_failures++;
        return;
    }

    conn.last_recv_time = client->current_time;
    conn.stats.on_packet_received(packet_len);

    if (pt_len < 8) {
        return;
    }

    AckFields ack_fields = read_ack_fields(plaintext);
    int newly_acked = conn.packet_tracker.process_acks(ack_fields);
    for (int i = 0; i < newly_acked; ++i) {
        conn.congestion.on_packet_acked();
    }

    double send_time = conn.packet_tracker.get_send_time(ack_fields.ack);
    if (send_time > 0.0) {
        conn.rtt.on_sample(send_time, client->current_time, ack_fields.ack_delay_us);
        conn.congestion.on_rtt_sample();
    }

    conn.packet_tracker.on_packet_received(0, client->current_time);

    int pos = 8;
    while (pos < pt_len) {
        uint8_t frame_type = plaintext[pos++];

        if (frame_type == wire::FRAME_UNRELIABLE_DATA && pos + 3 <= pt_len) {
            uint8_t ch = plaintext[pos++];
            uint16_t msg_len = 0;
            std::memcpy(&msg_len, plaintext + pos, 2);
            pos += 2;
            if (pos + msg_len > pt_len || ch >= conn.num_channels) {
                break;
            }
            DeliveredMessage dmsg;
            std::memcpy(dmsg.data, plaintext + pos, msg_len);
            dmsg.size = msg_len;
            dmsg.channel = ch;
            dmsg.valid = true;
            conn.delivered().push_back(dmsg);
            conn.stats.messages_received++;
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

            uint8_t ch_type = conn.ch(ch).type();
            if (ch_type == NETUDP_CHANNEL_RELIABLE_ORDERED) {
                if (conn.rs(ch).buffer_received_ordered(msg_seq, plaintext + pos, msg_len)) {
                    conn.rs(ch).deliver_ordered(
                        [&](const uint8_t* data, int len, uint16_t seq) {
                            DeliveredMessage dm;
                            int copy_len = std::min(len, static_cast<int>(NETUDP_MTU));
                            std::memcpy(dm.data, data, static_cast<size_t>(copy_len));
                            dm.size = copy_len;
                            dm.channel = ch;
                            dm.sequence = seq;
                            dm.valid = true;
                            conn.delivered().push_back(dm);
                            conn.stats.messages_received++;
                        });
                }
            } else if (ch_type == NETUDP_CHANNEL_RELIABLE_UNORDERED) {
                if (!conn.rs(ch).is_received_unordered(msg_seq)) {
                    conn.rs(ch).mark_received_unordered(msg_seq);
                    DeliveredMessage dm;
                    int copy_len = std::min(static_cast<int>(msg_len), static_cast<int>(NETUDP_MTU));
                    std::memcpy(dm.data, plaintext + pos, static_cast<size_t>(copy_len));
                    dm.size = copy_len;
                    dm.channel = ch;
                    dm.sequence = msg_seq;
                    dm.valid = true;
                    conn.delivered().push_back(dm);
                    conn.stats.messages_received++;
                }
            }
            pos += msg_len;

        } else if (frame_type == wire::FRAME_DISCONNECT) {
            client->state = ClientState::DISCONNECTED;
            conn.active = false;
            return;
        } else {
            break;
        }
    }
}

} // namespace netudp
