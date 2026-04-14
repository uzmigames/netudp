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
#include "core/hash_map.h"
#include "core/log.h"
#include "wire/frame.h"
#include "group/group.h"
#include "replication/schema.h"
#include "replication/entity.h"
#include "replication/replicate.h"
#include "profiling/profiler.h"
#include "reliability/packet_tracker.h"
#include "sim/network_sim.h"

#include <cstring>
#include <ctime>
#include <new>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

/* ======================================================================
 * IO Worker — one per thread, each owns a socket (SO_REUSEPORT on Linux)
 * ====================================================================== */

struct IOWorker {
    netudp::Socket socket;
    uint8_t recv_buf[NETUDP_MAX_PACKET_ON_WIRE] = {};
    uint8_t send_buf[NETUDP_MAX_PACKET_ON_WIRE] = {};
    uint8_t batch_storage[netudp::kSocketBatchMax][NETUDP_MAX_PACKET_ON_WIRE] = {};
    netudp::SocketPacket batch_pkts[netudp::kSocketBatchMax] = {};
    std::thread thread;
    std::atomic<bool> running{false};
    int thread_index = 0;
};

/* ======================================================================
 * Recv/Send pipeline queues (for PIPELINE threading mode)
 * ====================================================================== */

/** Queued inbound packet — copied from socket into recv ring. */
struct QueuedInPacket {
    netudp_address_t addr;
    uint8_t          data[NETUDP_MAX_PACKET_ON_WIRE];
    int              len = 0;
};

/** Queued outbound packet — ready to send via socket. */
struct QueuedOutPacket {
    netudp_address_t addr;
    uint8_t          data[NETUDP_MAX_PACKET_ON_WIRE];
    int              len = 0;
};

/** Thread-safe SPSC-style packet queue (single producer, single consumer). */
static constexpr int kPipelineQueueSize = 4096;

struct PipelineRecvQueue {
    QueuedInPacket  ring[kPipelineQueueSize];
    std::atomic<int> head{0};  /* Producer writes here */
    std::atomic<int> tail{0};  /* Consumer reads here */

    bool push(const netudp_address_t* addr, const uint8_t* data, int len) {
        int h = head.load(std::memory_order_relaxed);
        int next = (h + 1) % kPipelineQueueSize;
        if (next == tail.load(std::memory_order_acquire)) {
            return false; /* Full */
        }
        ring[h].addr = *addr;
        std::memcpy(ring[h].data, data, static_cast<size_t>(len));
        ring[h].len = len;
        head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(QueuedInPacket* out) {
        int t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) {
            return false; /* Empty */
        }
        *out = ring[t];
        tail.store((t + 1) % kPipelineQueueSize, std::memory_order_release);
        return true;
    }

    int count() const {
        int h = head.load(std::memory_order_relaxed);
        int t = tail.load(std::memory_order_relaxed);
        return (h >= t) ? (h - t) : (kPipelineQueueSize - t + h);
    }
};

struct PipelineSendQueue {
    QueuedOutPacket ring[kPipelineQueueSize];
    std::atomic<int> head{0};
    std::atomic<int> tail{0};

    bool push(const netudp_address_t* addr, const uint8_t* data, int len) {
        int h = head.load(std::memory_order_relaxed);
        int next = (h + 1) % kPipelineQueueSize;
        if (next == tail.load(std::memory_order_acquire)) {
            return false;
        }
        ring[h].addr = *addr;
        std::memcpy(ring[h].data, data, static_cast<size_t>(len));
        ring[h].len = len;
        head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(QueuedOutPacket* out) {
        int t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) {
            return false;
        }
        *out = ring[t];
        tail.store((t + 1) % kPipelineQueueSize, std::memory_order_release);
        return true;
    }
};

/* ======================================================================
 * Server struct — global scope to match forward decl in netudp_types.h
 * ====================================================================== */

struct netudp_server {
    bool running = false;
    int  max_clients = 0;

    netudp_server_config_t config = {};
    uint64_t protocol_id = 0;

    /* Primary socket (thread 0 / single-threaded mode) */
    netudp::Socket socket;
    netudp::Allocator allocator;
    netudp::RateLimiter rate_limiter;
    netudp::DDoSMonitor ddos;

    netudp::Connection* connections = nullptr;

    uint8_t challenge_key[32] = {};
    uint64_t challenge_sequence = 0;

    /* Fingerprint anti-replay: hash(8 bytes) → (address, expire_time).
     * O(1) lookup via FixedHashMap instead of O(N) linear scan. */
    struct FingerprintValue {
        netudp_address_t address;
        uint64_t expire_time;
    };
    netudp::FixedHashMap<uint64_t, FingerprintValue, 2048> fingerprint_map;

    double current_time = 0.0;
    double last_time = 0.0;
    double last_cleanup_time = 0.0;

    uint8_t recv_buf[NETUDP_MAX_PACKET_ON_WIRE] = {};
    uint8_t send_buf[NETUDP_MAX_PACKET_ON_WIRE] = {};

    /* Batch recv storage — kSocketBatchMax slots of MAX_PACKET_ON_WIRE each */
    uint8_t batch_storage[netudp::kSocketBatchMax][NETUDP_MAX_PACKET_ON_WIRE] = {};
    netudp::SocketPacket batch_pkts[netudp::kSocketBatchMax] = {};

    netudp::NetworkSimulator sim;
    bool sim_enabled = false;

    /* Packet handler dispatch table — indexed by first byte of message (0-255) */
    netudp_packet_handler_fn packet_handlers[256] = {};
    void*                    packet_handler_ctx[256] = {};

    /* Multi-thread I/O workers (nullptr in single-threaded mode) */
    IOWorker* io_workers = nullptr;
    int num_io_threads = 1;
    netudp_address_t bind_address = {};

    /* O(1) address → slot dispatch (replaces O(N) linear scan) */
    netudp::FixedHashMap<netudp_address_t, int, 4096> address_to_slot;

    /* Pipeline threading (recv thread + send thread + game thread) */
    bool pipeline_mode = false;
    PipelineRecvQueue* recv_queue = nullptr;
    PipelineSendQueue* send_queue = nullptr;
    std::thread recv_thread;
    std::thread send_thread;
    std::atomic<bool> pipeline_running{false};

    /* Active connection tracking — iterate O(active) not O(max_clients) */
    int* active_slots = nullptr;   /* Compact list of active slot indices */
    int  active_count = 0;
    int* free_slots = nullptr;     /* Stack of available slot indices */
    int  free_count = 0;
    int* slot_to_active = nullptr; /* Maps slot index → position in active_slots (-1 if inactive) */

    /* Multicast groups (phase 40) */
    netudp::Group* groups = nullptr;
    int  max_groups = 0;
    int* group_free_stack = nullptr; /* Free group index stack */
    int  group_free_count = 0;

    /* Per-group pre-allocated buffers (members[] + slot_to_pos[]) */
    int* group_member_pool = nullptr;   /* max_groups * max_clients ints for members */
    int* group_slot_pool = nullptr;     /* max_groups * max_clients ints for slot_to_pos */

    /* Packet pacing (phase 44) */
    int pacing_slices = 0;          /* 0 = burst (default), >0 = N slices per tick */
    int pacing_current_slice = 0;   /* Current slice being sent this tick */

    /* Property replication (phase 42) */
    static constexpr int kMaxSchemas = 64;
    static constexpr int kMaxEntities = 16384;
    netudp::Schema  schemas[kMaxSchemas] = {};
    int schema_count = 0;
    netudp::Entity* entities = nullptr;  /* Array of kMaxEntities */
    uint16_t next_entity_id = 1;         /* Auto-increment (0 = invalid) */

    /* Connection worker threads (phase 35) — parallel per-connection processing */
    int num_workers = 1;           /* 1 = single-threaded (default) */
    std::vector<std::thread> workers;
    std::atomic<int> workers_done{0};
    std::atomic<bool> workers_go{false};
    double worker_time = 0.0;
    double worker_dt = 0.0;
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
 * Pipeline thread functions
 * ====================================================================== */

/** Recv thread: tight loop pulling packets from socket into recv_queue. */
static void pipeline_recv_thread(netudp_server* server) {
    uint8_t batch_storage[netudp::kSocketBatchMax][NETUDP_MAX_PACKET_ON_WIRE] = {};
    netudp::SocketPacket batch_pkts[netudp::kSocketBatchMax] = {};

    while (server->pipeline_running.load(std::memory_order_relaxed)) {
        for (int i = 0; i < netudp::kSocketBatchMax; ++i) {
            batch_pkts[i].data = batch_storage[i];
        }

        int n = netudp::socket_recv_batch(&server->socket, batch_pkts,
                                           netudp::kSocketBatchMax,
                                           NETUDP_MAX_PACKET_ON_WIRE);
        if (n <= 0) {
            /* No data — yield briefly to avoid spinning */
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }

        for (int i = 0; i < n; ++i) {
            server->recv_queue->push(
                &batch_pkts[i].addr,
                static_cast<const uint8_t*>(batch_pkts[i].data),
                batch_pkts[i].len);
        }
    }
}

/** Send thread: drains send_queue and calls socket_send_batch (batched syscall). */
static void pipeline_send_thread(netudp_server* server) {
    /* Pre-allocate batch storage on thread stack */
    uint8_t batch_storage[netudp::kSocketBatchMax][NETUDP_MAX_PACKET_ON_WIRE] = {};
    netudp::SocketPacket batch[netudp::kSocketBatchMax] = {};
    for (int i = 0; i < netudp::kSocketBatchMax; ++i) {
        batch[i].data = batch_storage[i];
    }

    while (server->pipeline_running.load(std::memory_order_relaxed)) {
        NETUDP_ZONE("pipe::send_drain");
        QueuedOutPacket pkt;
        int count = 0;

        /* Drain up to kSocketBatchMax packets into batch array */
        for (int i = 0; i < netudp::kSocketBatchMax; ++i) {
            if (!server->send_queue->pop(&pkt)) {
                break;
            }
            batch[count].addr = pkt.addr;
            std::memcpy(batch[count].data, pkt.data, static_cast<size_t>(pkt.len));
            batch[count].len = pkt.len;
            ++count;
        }

        if (count > 0) {
            /* Batch send: sendmmsg on Linux, WSASendTo loop on Windows */
            netudp::socket_send_batch(&server->socket, batch, count);
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
}

/** Worker thread: processes a slice of active connections in parallel. */
static void connection_worker_fn(netudp_server* server, int worker_id) {
    while (server->running) {
        /* Spin-wait for go signal */
        while (!server->workers_go.load(std::memory_order_acquire)) {
            if (!server->running) { return; }
            std::this_thread::yield();
        }

        double time = server->worker_time;
        double dt = server->worker_dt;
        int total = server->active_count;
        int nw = server->num_workers;

        /* This worker processes slice [start, end) of active_slots */
        int slice = (total + nw - 1) / nw;
        int start = worker_id * slice;
        int end = start + slice;
        if (end > total) { end = total; }

        for (int a = start; a < end; ++a) {
            int i = server->active_slots[a];
            netudp::Connection& conn = server->connections[i];

            /* Fast-path: idle peer */
            bool needs_keepalive = (time - conn.last_send_time > 1.0);
            if (conn.pending_mask == 0 && !needs_keepalive) {
                if (time >= conn.next_slow_tick) {
                    conn.next_slow_tick = time + 0.1;
                    if (conn.cdata != nullptr) { conn.frag().cleanup_timeout(time); }
                    conn.congestion.evaluate();
                }
                continue;
            }

            conn.bandwidth.refill(time);
            conn.budget.refill(dt, conn.congestion.send_rate());
            netudp::server_send_pending(server, i);

            if (needs_keepalive) {
                netudp::server_send_keepalive(server, i);
            }

            if (time >= conn.next_slow_tick) {
                conn.next_slow_tick = time + 0.1;
                conn.stats.update_throughput(time);
                conn.stats.ping_ms = conn.rtt.ping_ms();
                conn.stats.send_rate_bytes_per_sec = conn.congestion.send_rate();
                conn.stats.max_send_rate_bytes_per_sec = conn.congestion.max_send_rate();
                if (conn.cdata != nullptr) { conn.frag().cleanup_timeout(time); }
                conn.congestion.evaluate();
            }
        }

        /* Signal done */
        server->workers_done.fetch_add(1, std::memory_order_release);

        /* Wait for main thread to reset go signal */
        while (server->workers_go.load(std::memory_order_acquire)) {
            if (!server->running) { return; }
            std::this_thread::yield();
        }
    }
}

/** Send a packet: either directly via socket or via send_queue in pipeline mode.
 *  cached_sa/cached_sa_len: pre-built sockaddr from Connection (phase 38).
 *  In non-pipeline mode, skips address_to_sockaddr entirely. */
static void server_send_packet(netudp_server* server,
                                const netudp_address_t* dest,
                                const uint8_t* cached_sa, int cached_sa_len,
                                const uint8_t* data, int len) {
    NETUDP_ZONE("srv::send_pkt");
    if (server->pipeline_mode && server->send_queue != nullptr) {
        server->send_queue->push(dest, data, len);
    } else {
        netudp::socket_send_raw(&server->socket, cached_sa, cached_sa_len, data, len);
    }
}

/** Remove slot from active list (swap-remove O(1)) and push to free stack. */
static void server_deactivate_slot(netudp_server* server, int slot) {
    int active_pos = server->slot_to_active[slot];
    if (active_pos < 0) { return; }

    /* Swap-remove: move last active into this position */
    int last = --server->active_count;
    if (active_pos < last) {
        int moved_slot = server->active_slots[last];
        server->active_slots[active_pos] = moved_slot;
        server->slot_to_active[moved_slot] = active_pos;
    }
    server->slot_to_active[slot] = -1;

    /* Push slot back to free stack */
    server->free_slots[server->free_count++] = slot;

    /* Remove from all multicast groups (phase 40) */
    if (server->groups != nullptr) {
        for (int g = 0; g < server->max_groups; ++g) {
            if (server->groups[g].active) {
                server->groups[g].remove(slot);
            }
        }
    }
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
    server->bind_address = bind_addr;

    /* Determine thread count (clamped to 1..16) */
    int num_threads = config->num_io_threads;
    if (num_threads <= 0) { num_threads = 1; }
    if (num_threads > 16) { num_threads = 16; }
    server->num_io_threads = num_threads;

    /* Create primary socket (thread 0) */
    int sock_flags = 0;
#ifdef __linux__
    if (num_threads > 1) { sock_flags = netudp::kSocketFlagReusePort; }
#endif
    if (netudp::socket_create(&server->socket, &bind_addr,
                               4 * 1024 * 1024, 4 * 1024 * 1024, sock_flags) != NETUDP_OK) {
        delete server;
        return nullptr;
    }
    /* Pin primary socket to CPU 0 for RSS distribution */
    if (num_threads > 1) {
        netudp::socket_set_cpu_affinity(&server->socket, 0);
    }

    /* Create additional IO worker sockets for multi-threaded mode */
    if (num_threads > 1) {
        server->io_workers = new (std::nothrow) IOWorker[static_cast<size_t>(num_threads - 1)];
        if (server->io_workers == nullptr) {
            netudp::socket_destroy(&server->socket);
            delete server;
            return nullptr;
        }
        for (int i = 0; i < num_threads - 1; ++i) {
            server->io_workers[i].thread_index = i + 1;
            if (netudp::socket_create(&server->io_workers[i].socket, &bind_addr,
                                       4 * 1024 * 1024, 4 * 1024 * 1024, sock_flags) != NETUDP_OK) {
                /* Cleanup already-created worker sockets */
                for (int j = 0; j < i; ++j) {
                    netudp::socket_destroy(&server->io_workers[j].socket);
                }
                delete[] server->io_workers;
                server->io_workers = nullptr;
                netudp::socket_destroy(&server->socket);
                delete server;
                return nullptr;
            }
            /* Auto CPU affinity: pin worker socket N to CPU N+1 (CPU 0 = primary) */
            netudp::socket_set_cpu_affinity(&server->io_workers[i].socket, i + 1);
        }
    }

    /* Wire network simulator if a config was provided. */
    if (config->sim_config != nullptr) {
        server->sim.init(
            *static_cast<const netudp::NetSimConfig*>(config->sim_config));
        server->sim_enabled = true;
    }

    return server;
}

void netudp_server_start(netudp_server_t* server, int max_clients) {
    if (server == nullptr || max_clients <= 0) {
        return;
    }

    server->max_clients = max_clients;
    server->running = true;

    /* Connection requires alignas(64) — use aligned allocation */
    {
        size_t conn_bytes = sizeof(netudp::Connection) * static_cast<size_t>(max_clients);
#ifdef NETUDP_PLATFORM_WINDOWS
        server->connections = static_cast<netudp::Connection*>(
            _aligned_malloc(conn_bytes, alignof(netudp::Connection)));
#else
        server->connections = static_cast<netudp::Connection*>(
            std::aligned_alloc(alignof(netudp::Connection), conn_bytes));
#endif
    }
    if (server->connections != nullptr) {
        for (int i = 0; i < max_clients; ++i) {
            new (&server->connections[i]) netudp::Connection();
        }
    }

    /* Initialize active/free slot tracking */
    auto slot_bytes = static_cast<size_t>(max_clients) * sizeof(int);
    server->active_slots = static_cast<int*>(server->allocator.allocate(slot_bytes));
    server->free_slots = static_cast<int*>(server->allocator.allocate(slot_bytes));
    server->slot_to_active = static_cast<int*>(server->allocator.allocate(slot_bytes));
    server->active_count = 0;
    server->free_count = max_clients;
    for (int i = 0; i < max_clients; ++i) {
        server->free_slots[i] = max_clients - 1 - i; /* Stack: top = slot 0 */
        server->slot_to_active[i] = -1;
    }

    /* Multicast groups (phase 40) */
    {
        int mg = server->config.max_groups;
        if (mg <= 0) { mg = 256; }
        server->max_groups = mg;
        server->groups = new (std::nothrow) netudp::Group[static_cast<size_t>(mg)]();
        server->group_free_stack = new (std::nothrow) int[static_cast<size_t>(mg)];
        server->group_free_count = mg;
        for (int i = 0; i < mg; ++i) {
            server->group_free_stack[i] = mg - 1 - i; /* Top = index 0 */
        }

        /* Pre-allocate per-group member + slot_to_pos buffers */
        int cap = (max_clients < netudp::kMaxGroupMembers) ? max_clients : netudp::kMaxGroupMembers;
        size_t pool_ints = static_cast<size_t>(mg) * static_cast<size_t>(cap);
        server->group_member_pool = new (std::nothrow) int[pool_ints];
        size_t slot_ints = static_cast<size_t>(mg) * static_cast<size_t>(max_clients);
        server->group_slot_pool = new (std::nothrow) int[slot_ints];
    }

    /* Packet pacing (phase 44) */
    server->pacing_slices = server->config.pacing_slices;
    server->pacing_current_slice = 0;

    /* Entity pool (phase 42) */
    server->entities = new (std::nothrow) netudp::Entity[netudp_server::kMaxEntities]();

    netudp::crypto::random_bytes(server->challenge_key, 32);
    server->challenge_sequence = 0;

    /* Start connection worker threads if num_io_threads >= 3
     * (1 = single, 2 = pipeline recv/send, 3+ = pipeline + N-2 workers) */
    if (server->num_io_threads >= 3) {
        server->num_workers = server->num_io_threads - 1; /* Reserve 1 for pipeline */
        if (server->num_workers > 8) { server->num_workers = 8; }
        server->workers_go.store(false, std::memory_order_relaxed);
        server->workers_done.store(0, std::memory_order_relaxed);
        for (int w = 0; w < server->num_workers; ++w) {
            server->workers.emplace_back(connection_worker_fn, server, w);
        }
        NLOG_INFO("[netudp] worker threads: %d started", server->num_workers);
    }

    /* Start pipeline threads if num_io_threads >= 2 */
    if (server->num_io_threads >= 2) {
        server->pipeline_mode = true;
        server->recv_queue = new (std::nothrow) PipelineRecvQueue();
        server->send_queue = new (std::nothrow) PipelineSendQueue();
        if (server->recv_queue != nullptr && server->send_queue != nullptr) {
            server->pipeline_running.store(true, std::memory_order_release);
            server->recv_thread = std::thread(pipeline_recv_thread, server);
            server->send_thread = std::thread(pipeline_send_thread, server);
            NLOG_INFO("[netudp] pipeline mode: recv + send threads started");
        }
    }
}

void netudp_server_stop(netudp_server_t* server) {
    if (server == nullptr) {
        return;
    }
    server->running = false;

    /* Stop worker threads */
    if (!server->workers.empty()) {
        server->workers_go.store(false, std::memory_order_release);
        for (auto& w : server->workers) {
            if (w.joinable()) { w.join(); }
        }
        server->workers.clear();
        server->num_workers = 1;
    }

    /* Stop pipeline threads */
    if (server->pipeline_mode) {
        server->pipeline_running.store(false, std::memory_order_release);
        if (server->recv_thread.joinable()) { server->recv_thread.join(); }
        if (server->send_thread.joinable()) { server->send_thread.join(); }
        delete server->recv_queue;
        delete server->send_queue;
        server->recv_queue = nullptr;
        server->send_queue = nullptr;
        server->pipeline_mode = false;
    }

    server->address_to_slot.clear();

    if (server->connections != nullptr) {
        for (int i = 0; i < server->max_clients; ++i) {
            server->connections[i].reset();
        }
        /* Must match aligned allocation in server_start */
#ifdef NETUDP_PLATFORM_WINDOWS
        _aligned_free(server->connections);
#else
        std::free(server->connections);
#endif
        server->connections = nullptr;
    }
    if (server->active_slots != nullptr) {
        server->allocator.deallocate(server->active_slots);
        server->active_slots = nullptr;
    }
    if (server->free_slots != nullptr) {
        server->allocator.deallocate(server->free_slots);
        server->free_slots = nullptr;
    }
    if (server->slot_to_active != nullptr) {
        server->allocator.deallocate(server->slot_to_active);
        server->slot_to_active = nullptr;
    }
    server->active_count = 0;
    server->free_count = 0;

    /* Free entity pool */
    delete[] server->entities;
    server->entities = nullptr;
    server->schema_count = 0;
    server->next_entity_id = 1;

    /* Free multicast groups */
    delete[] server->groups;
    server->groups = nullptr;
    delete[] server->group_free_stack;
    server->group_free_stack = nullptr;
    delete[] server->group_member_pool;
    server->group_member_pool = nullptr;
    delete[] server->group_slot_pool;
    server->group_slot_pool = nullptr;
    server->max_groups = 0;
    server->group_free_count = 0;
}

int netudp_server_max_clients(const netudp_server_t* server) {
    if (server == nullptr) { return 0; }
    return server->max_clients;
}

void netudp_server_get_stats(const netudp_server_t* server,
                             netudp_server_stats_t* out) {
    if (server == nullptr || out == nullptr) { return; }
    std::memset(out, 0, sizeof(*out));

    out->max_clients = server->max_clients;
    out->ddos_severity = static_cast<uint8_t>(server->ddos.severity());

    int connected = 0;
    uint64_t bytes_recv = 0;
    uint64_t bytes_sent = 0;
    double   pps_in  = 0.0;
    double   pps_out = 0.0;

    for (int i = 0; i < server->max_clients; ++i) {
        const netudp::Connection& c = server->connections[i];
        if (!c.active) { continue; }
        ++connected;
        bytes_recv += static_cast<uint64_t>(c.stats.in_bytes_per_sec);
        bytes_sent += static_cast<uint64_t>(c.stats.out_bytes_per_sec);
        pps_in     += static_cast<double>(c.stats.in_packets_per_sec);
        pps_out    += static_cast<double>(c.stats.out_packets_per_sec);
    }

    out->connected_clients = connected;
    out->total_bytes_recv  = bytes_recv;
    out->total_bytes_sent  = bytes_sent;
    out->recv_pps          = pps_in;
    out->send_pps          = pps_out;
}

int netudp_server_num_io_threads(const netudp_server_t* server) {
    if (server == nullptr) { return 0; }
    return server->num_io_threads;
}

int netudp_server_set_thread_affinity(netudp_server_t* server,
                                       int thread_index, int cpu_id) {
    if (server == nullptr) { return NETUDP_ERROR_INVALID_PARAM; }
    if (thread_index < 0 || thread_index >= server->num_io_threads) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    if (cpu_id >= 0) {
        CPU_SET(cpu_id, &cpuset);
    }

    /* Thread 0 uses the main thread — set affinity on current thread */
    if (thread_index == 0) {
        if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
            return NETUDP_ERROR_SOCKET;
        }
        return NETUDP_OK;
    }

    /* Worker threads (1..N-1) — set affinity on the worker's thread */
    int worker_idx = thread_index - 1;
    if (server->io_workers == nullptr || !server->io_workers[worker_idx].running.load()) {
        return NETUDP_ERROR_INVALID_PARAM;
    }
    auto native = server->io_workers[worker_idx].thread.native_handle();
    if (pthread_setaffinity_np(native, sizeof(cpuset), &cpuset) != 0) {
        return NETUDP_ERROR_SOCKET;
    }
    return NETUDP_OK;
#else
    /* Windows / macOS — not implemented yet */
    (void)thread_index;
    (void)cpu_id;
    return NETUDP_OK;
#endif
}

int netudp_windows_is_wfp_active() {
#ifdef NETUDP_PLATFORM_WINDOWS
    /* Check if Base Filtering Engine (BFE) service is running.
     * BFE is the WFP service — when active, every packet traverses WFP callouts
     * adding ~2µs overhead. Stopping BFE (net stop BFE) removes this cost. */
    SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) { return -1; }

    SC_HANDLE svc = OpenService(scm, TEXT("BFE"), SERVICE_QUERY_STATUS);
    if (svc == nullptr) {
        CloseServiceHandle(scm);
        return -1;
    }

    SERVICE_STATUS status = {};
    int result = 0;
    if (QueryServiceStatus(svc, &status)) {
        result = (status.dwCurrentState == SERVICE_RUNNING) ? 1 : 0;
    } else {
        result = -1;
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return result;
#else
    return 0;
#endif
}

/* Dispatch a fully-received (or sim-delivered) packet to the correct handler. */
static void server_dispatch_packet(netudp_server* server,
                                   const netudp_address_t* from,
                                   const uint8_t* data, int len) {
    NETUDP_ZONE("srv::dispatch");
    uint8_t prefix      = data[0];
    uint8_t packet_type = prefix & 0x0F;

    if (packet_type == 0x00 && len == 1078) {
        if (!server->ddos.should_process_new_connection()) {
            NLOG_WARN("[netudp] dispatch: DDoS threshold exceeded, dropping connection request");
            return;
        }
        netudp::server_handle_connection_request(server, from, data, len);
    } else if ((packet_type >= 0x04 && packet_type <= 0x06) ||
               prefix == netudp::crypto::PACKET_PREFIX_DATA_REKEY) {
        /* Phase 36: O(1) dispatch via slot_id embedded in packet header.
         * Wire: [prefix(1)][slot_id(2)][ciphertext(N)]
         * Minimum: prefix + slot_id + at least 1 byte ciphertext = 4 bytes. */
        int slot = -1;
        if (len >= 4) {
            uint16_t wire_slot = 0;
            std::memcpy(&wire_slot, data + 1, 2);
            /* Validate: slot_id in range, connection active, address matches */
            if (wire_slot < static_cast<uint16_t>(server->max_clients)) {
                netudp::Connection& cand = server->connections[wire_slot];
                if (cand.active && netudp_address_equal(&cand.address, from)) {
                    slot = static_cast<int>(wire_slot);
                }
            }
        }
        /* Fallback: if slot_id invalid or address mismatch, use hash map */
        if (slot < 0) {
            const int* slot_ptr = server->address_to_slot.find(*from);
            if (slot_ptr != nullptr) {
                slot = *slot_ptr;
            }
        }
        if (slot >= 0) {
            netudp::server_handle_data_packet(server, slot, data, len);
        } else {
            NLOG_DEBUG("[netudp] dispatch: data packet from unknown address (prefix=0x%02x)", prefix);
            server->ddos.on_bad_packet();
        }
    } else {
        NLOG_DEBUG("[netudp] dispatch: unknown packet type (prefix=0x%02x, len=%d)", prefix, len);
        server->ddos.on_bad_packet();
    }
}

/* Callback adapter for NetworkSimulator::poll(). */
struct SimDispatchCtx { netudp_server* server; };

static void sim_deliver_cb(void* ctx, const uint8_t* data, int len,
                            const netudp_address_t* from) {
    auto* dc = static_cast<SimDispatchCtx*>(ctx);
    server_dispatch_packet(dc->server, from, data, len);
}

void netudp_server_update(netudp_server_t* server, double time) {
    NETUDP_ZONE("srv::update");
    if (server == nullptr || !server->running) {
        return;
    }

    double dt = time - server->current_time;
    server->current_time = time;

    /* DDoS monitor tick */
    server->ddos.update(dt);

    /* Receive packets — pipeline mode drains from recv_queue,
     * single-thread mode calls socket_recv_batch directly. */
    if (server->pipeline_mode && server->recv_queue != nullptr) {
        /* Pipeline: drain recv_queue filled by recv thread */
        QueuedInPacket qpkt;
        int drained = 0;
        while (server->recv_queue->pop(&qpkt) && drained < 4096) {
            const int* known_pipe = server->address_to_slot.find(qpkt.addr);
            if (known_pipe == nullptr && !server->rate_limiter.allow(&qpkt.addr, time)) {
                server->ddos.on_bad_packet();
            } else if (server->sim_enabled) {
                server->sim.submit(qpkt.data, qpkt.len, &qpkt.addr, time);
            } else {
                server_dispatch_packet(server, &qpkt.addr, qpkt.data, qpkt.len);
            }
            ++drained;
        }
    } else {
        /* Single-thread: recv directly from socket */
        for (;;) {
            for (int i = 0; i < netudp::kSocketBatchMax; ++i) {
                server->batch_pkts[i].data = server->batch_storage[i];
            }

            int n = netudp::socket_recv_batch(&server->socket, server->batch_pkts,
                                              netudp::kSocketBatchMax,
                                              NETUDP_MAX_PACKET_ON_WIRE);
            if (n <= 0) {
                break;
            }

            for (int i = 0; i < n; ++i) {
                netudp_address_t* from    = &server->batch_pkts[i].addr;
                const void*       pkt_buf = server->batch_pkts[i].data;
                int               pkt_len = server->batch_pkts[i].len;

                /* Fast-path: known peers bypass rate limiter (ENet pattern).
                 * Rate limiting only applies to unknown/unauthenticated packets. */
                const int* known = server->address_to_slot.find(*from);
                if (known == nullptr) {
                    /* Unknown address — rate limit before processing */
                    if (!server->rate_limiter.allow(from, time)) {
                        server->ddos.on_bad_packet();
                        continue;
                    }
                }

                if (server->sim_enabled) {
                    server->sim.submit(static_cast<const uint8_t*>(pkt_buf), pkt_len, from, time);
                } else {
                    server_dispatch_packet(server, from,
                                           static_cast<const uint8_t*>(pkt_buf), pkt_len);
                }
            }

            if (n < netudp::kSocketBatchMax) {
                break;
            }
        }

        /* Drain additional IO worker sockets (multi-socket, single-thread mode) */
        for (int w = 0; w < server->num_io_threads - 1; ++w) {
            IOWorker& worker = server->io_workers[w];
            for (;;) {
                for (int i = 0; i < netudp::kSocketBatchMax; ++i) {
                    worker.batch_pkts[i].data = worker.batch_storage[i];
                }
                int n = netudp::socket_recv_batch(&worker.socket, worker.batch_pkts,
                                                  netudp::kSocketBatchMax,
                                                  NETUDP_MAX_PACKET_ON_WIRE);
                if (n <= 0) { break; }
                for (int i = 0; i < n; ++i) {
                    netudp_address_t* from    = &worker.batch_pkts[i].addr;
                    const void*       pkt_buf = worker.batch_pkts[i].data;
                    int               pkt_len = worker.batch_pkts[i].len;
                    if (!server->rate_limiter.allow(from, time)) {
                        server->ddos.on_bad_packet();
                        continue;
                    }
                    if (server->sim_enabled) {
                        server->sim.submit(static_cast<const uint8_t*>(pkt_buf),
                                           pkt_len, from, time);
                    } else {
                        server_dispatch_packet(server, from,
                                               static_cast<const uint8_t*>(pkt_buf), pkt_len);
                    }
                }
                if (n < netudp::kSocketBatchMax) { break; }
            }
        }
    }

    /* Drain simulator-buffered packets that are now due. */
    if (server->sim_enabled) {
        SimDispatchCtx dc{server};
        server->sim.poll(time, &dc, sim_deliver_cb);
    }

    /* Timeout check — must run on main thread (modifies active_slots) */
    for (int a = 0; a < server->active_count; ) {
        int i = server->active_slots[a];
        netudp::Connection& conn = server->connections[i];
        if (time - conn.last_recv_time > conn.timeout_seconds) {
            NLOG_WARN("[netudp] client %llu timed out (slot=%d, idle=%.1fs)",
                            (unsigned long long)conn.client_id, i,
                            time - conn.last_recv_time);
            if (server->config.on_disconnect != nullptr) {
                server->config.on_disconnect(server->config.callback_context, i, -4);
            }
            server->address_to_slot.remove(conn.address);
            server_deactivate_slot(server, i);
            conn.reset();
            continue;
        }
        ++a;
    }

    /* Per-connection processing: bandwidth, send_pending, keepalive, slow tick.
     * When num_workers > 1, dispatch to worker threads for parallel processing. */
    if (server->num_workers > 1 && server->active_count > 0) {
        /* Parallel: signal workers, wait for completion */
        server->worker_time = time;
        server->worker_dt = dt;
        server->workers_done.store(0, std::memory_order_release);
        server->workers_go.store(true, std::memory_order_release);

        /* Wait for all workers to finish */
        while (server->workers_done.load(std::memory_order_acquire) < server->num_workers) {
            std::this_thread::yield();
        }
        server->workers_go.store(false, std::memory_order_release);
    } else {
        /* Single-threaded: process connections inline.
         * Phase 44: pacing divides active connections into N slices.
         * Each server_update call processes one slice, round-robin. */
        int slice_start = 0;
        int slice_end = server->active_count;

        if (server->pacing_slices > 1 && server->active_count > server->pacing_slices) {
            int per_slice = server->active_count / server->pacing_slices;
            int remainder = server->active_count % server->pacing_slices;
            int s = server->pacing_current_slice % server->pacing_slices;

            slice_start = s * per_slice + (s < remainder ? s : remainder);
            int this_slice = per_slice + (s < remainder ? 1 : 0);
            slice_end = slice_start + this_slice;

            server->pacing_current_slice = (s + 1) % server->pacing_slices;
        }

        for (int a = slice_start; a < slice_end; ++a) {
            int i = server->active_slots[a];
            netudp::Connection& conn = server->connections[i];

            /* Fast-path: idle peer with nothing to send and no keepalive due.
             * ENet does ~8 inline ops for idle peers. We do 2 comparisons. */
            bool needs_keepalive = (time - conn.last_send_time > 1.0);
            if (conn.pending_mask == 0 && !needs_keepalive) {
                /* Only slow tick check for idle peers */
                if (time >= conn.next_slow_tick) {
                    conn.next_slow_tick = time + 0.1;
                    conn.stats.update_throughput(time);
                    conn.stats.ping_ms = conn.rtt.ping_ms();
                    conn.stats.send_rate_bytes_per_sec = conn.congestion.send_rate();
                    conn.stats.max_send_rate_bytes_per_sec = conn.congestion.max_send_rate();
                    if (conn.cdata != nullptr) { conn.frag().cleanup_timeout(time); }
                    conn.congestion.evaluate();
                }
                continue;
            }

            /* Full path: peer has data to send or keepalive due */
            conn.bandwidth.refill(time);
            conn.budget.refill(dt, conn.congestion.send_rate());
            netudp::server_send_pending(server, i);

            if (needs_keepalive) {
                netudp::server_send_keepalive(server, i);
            }

            if (time >= conn.next_slow_tick) {
                conn.next_slow_tick = time + 0.1;
                conn.stats.update_throughput(time);
                conn.stats.ping_ms = conn.rtt.ping_ms();
                conn.stats.send_rate_bytes_per_sec = conn.congestion.send_rate();
                conn.stats.max_send_rate_bytes_per_sec = conn.congestion.max_send_rate();
                if (conn.cdata != nullptr) { conn.frag().cleanup_timeout(time); }
                conn.congestion.evaluate();
            }
        }
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

    /* Destroy IO worker sockets */
    if (server->io_workers != nullptr) {
        for (int i = 0; i < server->num_io_threads - 1; ++i) {
            netudp::socket_destroy(&server->io_workers[i].socket);
        }
        delete[] server->io_workers;
        server->io_workers = nullptr;
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
    NETUDP_ZONE("srv::send");
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
    if (!conn.ch(channel).queue_send(static_cast<const uint8_t*>(data), bytes, flags)) {
        return NETUDP_ERROR_NO_BUFFERS;
    }
    conn.pending_mask |= static_cast<uint8_t>(1U << channel);
    conn.ch(channel).start_nagle(server->current_time);

    /* If NO_DELAY, flush immediately */
    if ((flags & NETUDP_SEND_NO_DELAY) != 0) {
        conn.ch(channel).flush();
        netudp::server_send_pending(server, client_index);
    }

    return NETUDP_OK;
}

int netudp_server_send_state(netudp_server_t* server, int client_index,
                             int channel, uint16_t entity_id,
                             const void* data, int bytes) {
    NETUDP_ZONE("srv::send_state");
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

    /* State overwrite only on unreliable channels */
    uint8_t ch_type = conn.ch(channel).type();
    if (ch_type == NETUDP_CHANNEL_RELIABLE_ORDERED ||
        ch_type == NETUDP_CHANNEL_RELIABLE_UNORDERED) {
        return NETUDP_ERROR_INVALID_PARAM;
    }

    if (!conn.ch(channel).queue_send_state(
            static_cast<const uint8_t*>(data), bytes, 0, entity_id)) {
        return NETUDP_ERROR_NO_BUFFERS;
    }
    conn.pending_mask |= static_cast<uint8_t>(1U << channel);
    conn.ch(channel).start_nagle(server->current_time);
    return NETUDP_OK;
}

void netudp_group_send_state(netudp_server_t* server, int group_id,
                             int channel, uint16_t entity_id,
                             const void* data, int bytes) {
    NETUDP_ZONE("srv::group_send_state");
    if (server == nullptr || !server->running) {
        return;
    }
    if (group_id < 0 || group_id >= server->max_groups) {
        return;
    }
    const netudp::Group& g = server->groups[group_id];
    if (!g.active) {
        return;
    }
    for (int i = 0; i < g.member_count; ++i) {
        netudp_server_send_state(server, g.members[i], channel, entity_id, data, bytes);
    }
}

/* ======================================================================
 * Receive — deliver messages from connection's delivered_messages queue
 * ====================================================================== */

int netudp_server_receive(netudp_server_t* server, int client_index,
                          netudp_message_t** messages, int max_messages) {
    NETUDP_ZONE("srv::receive");
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
    while (count < max_messages && !conn.delivered().is_empty()) {
        netudp::DeliveredMessage dmsg;
        if (!conn.delivered().pop_front(&dmsg)) {
            break;
        }
        if (!dmsg.valid) {
            continue;
        }

        /* Packet handler dispatch: if first byte matches a registered handler,
         * invoke it directly and skip the normal app-facing message queue.    */
        if (dmsg.size >= 1) {
            uint8_t ptype = dmsg.data[0];
            netudp_packet_handler_fn handler = server->packet_handlers[ptype];
            if (handler != nullptr) {
                handler(server->packet_handler_ctx[ptype],
                        client_index, dmsg.data, dmsg.size, dmsg.channel);
                continue; /* consumed — do not expose to app */
            }
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

int netudp_server_receive_batch(netudp_server_t* server,
                                netudp_message_t** out, int max_messages) {
    NETUDP_ZONE("srv::receive_batch");
    if (server == nullptr || out == nullptr || max_messages <= 0) {
        return NETUDP_ERROR_INVALID_PARAM;
    }
    int total = 0;
    /* Iterate active connections only — O(active) not O(max_clients).
     * With 5000 slots and 100 active, this is 50x fewer iterations. */
    for (int a = 0; a < server->active_count && total < max_messages; ++a) {
        int slot = server->active_slots[a];
        int n = netudp_server_receive(server, slot, out + total, max_messages - total);
        if (n > 0) { total += n; }
    }
    return total;
}

/* ======================================================================
 * Broadcast / Flush
 * ====================================================================== */

void netudp_server_broadcast(netudp_server_t* server, int channel,
                             const void* data, int bytes, int flags) {
    NETUDP_ZONE("srv::broadcast");
    if (server == nullptr || !server->running) {
        return;
    }
    /* Iterate active connections only — O(active) not O(max_clients) */
    for (int a = 0; a < server->active_count; ++a) {
        int slot = server->active_slots[a];
        netudp_server_send(server, slot, channel, data, bytes, flags);
    }
}

void netudp_server_broadcast_except(netudp_server_t* server, int except_client,
                                    int channel, const void* data, int bytes, int flags) {
    NETUDP_ZONE("srv::broadcast_except");
    if (server == nullptr || !server->running) {
        return;
    }
    for (int a = 0; a < server->active_count; ++a) {
        int slot = server->active_slots[a];
        if (slot != except_client) {
            netudp_server_send(server, slot, channel, data, bytes, flags);
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
        conn.ch(ch).flush();
    }
    netudp::server_send_pending(server, client_index);
}

/* ======================================================================
 * Multicast Groups (phase 40)
 * ====================================================================== */

int netudp_group_create(netudp_server_t* server) {
    if (server == nullptr || !server->running) {
        return -1;
    }
    if (server->group_free_count <= 0) {
        return -1; /* No free group slots */
    }

    int idx = server->group_free_stack[--server->group_free_count];
    int cap = (server->max_clients < netudp::kMaxGroupMembers)
              ? server->max_clients : netudp::kMaxGroupMembers;

    int* member_buf = server->group_member_pool +
                      static_cast<size_t>(idx) * static_cast<size_t>(cap);
    int* slot_buf   = server->group_slot_pool +
                      static_cast<size_t>(idx) * static_cast<size_t>(server->max_clients);

    server->groups[idx].init(idx, server->max_clients, member_buf, slot_buf, cap);
    return idx;
}

void netudp_group_destroy(netudp_server_t* server, int group_id) {
    if (server == nullptr || group_id < 0 || group_id >= server->max_groups) {
        return;
    }
    netudp::Group& g = server->groups[group_id];
    if (!g.active) {
        return;
    }
    g.reset();
    server->group_free_stack[server->group_free_count++] = group_id;
}

int netudp_group_add(netudp_server_t* server, int group_id, int client_index) {
    if (server == nullptr || group_id < 0 || group_id >= server->max_groups) {
        return NETUDP_ERROR_INVALID_PARAM;
    }
    netudp::Group& g = server->groups[group_id];
    if (!g.active) {
        return NETUDP_ERROR_INVALID_PARAM;
    }
    if (client_index < 0 || client_index >= server->max_clients) {
        return NETUDP_ERROR_INVALID_PARAM;
    }
    if (!server->connections[client_index].active) {
        return NETUDP_ERROR_NOT_CONNECTED;
    }
    return g.add(client_index) ? NETUDP_OK : NETUDP_ERROR_NO_BUFFERS;
}

int netudp_group_remove(netudp_server_t* server, int group_id, int client_index) {
    if (server == nullptr || group_id < 0 || group_id >= server->max_groups) {
        return NETUDP_ERROR_INVALID_PARAM;
    }
    netudp::Group& g = server->groups[group_id];
    if (!g.active) {
        return NETUDP_ERROR_INVALID_PARAM;
    }
    return g.remove(client_index) ? NETUDP_OK : NETUDP_ERROR_INVALID_PARAM;
}

void netudp_group_send(netudp_server_t* server, int group_id,
                       int channel, const void* data, int bytes, int flags) {
    NETUDP_ZONE("srv::group_send");
    if (server == nullptr || !server->running) {
        return;
    }
    if (group_id < 0 || group_id >= server->max_groups) {
        return;
    }
    const netudp::Group& g = server->groups[group_id];
    if (!g.active) {
        return;
    }
    for (int i = 0; i < g.member_count; ++i) {
        netudp_server_send(server, g.members[i], channel, data, bytes, flags);
    }
}

void netudp_group_send_except(netudp_server_t* server, int group_id, int except_client,
                              int channel, const void* data, int bytes, int flags) {
    NETUDP_ZONE("srv::group_send_except");
    if (server == nullptr || !server->running) {
        return;
    }
    if (group_id < 0 || group_id >= server->max_groups) {
        return;
    }
    const netudp::Group& g = server->groups[group_id];
    if (!g.active) {
        return;
    }
    for (int i = 0; i < g.member_count; ++i) {
        if (g.members[i] != except_client) {
            netudp_server_send(server, g.members[i], channel, data, bytes, flags);
        }
    }
}

int netudp_group_count(const netudp_server_t* server, int group_id) {
    if (server == nullptr || group_id < 0 || group_id >= server->max_groups) {
        return 0;
    }
    const netudp::Group& g = server->groups[group_id];
    return g.active ? g.member_count : 0;
}

int netudp_group_has(const netudp_server_t* server, int group_id, int client_index) {
    if (server == nullptr || group_id < 0 || group_id >= server->max_groups) {
        return 0;
    }
    return server->groups[group_id].has(client_index) ? 1 : 0;
}

/* ======================================================================
 * Property Replication API (phase 42)
 * ====================================================================== */

static netudp::Entity* find_entity(netudp_server_t* s, uint16_t eid) {
    if (s == nullptr || s->entities == nullptr || eid == 0) { return nullptr; }
    for (int i = 0; i < netudp_server::kMaxEntities; ++i) {
        if (s->entities[i].active && s->entities[i].entity_id == eid) {
            return &s->entities[i];
        }
    }
    return nullptr;
}

int netudp_schema_create(netudp_server_t* server) {
    if (server == nullptr) { return -1; }
    if (server->schema_count >= netudp_server::kMaxSchemas) { return -1; }
    int id = server->schema_count++;
    server->schemas[id] = netudp::Schema{};
    server->schemas[id].schema_id = id;
    return id;
}

void netudp_schema_destroy(netudp_server_t* server, int schema_id) {
    if (server == nullptr || schema_id < 0 || schema_id >= server->schema_count) { return; }
    server->schemas[schema_id] = netudp::Schema{};
    server->schemas[schema_id].schema_id = -1;
}

int netudp_schema_add_u8(netudp_server_t* s, int sid, const char* name, uint16_t flags) {
    if (s == nullptr || sid < 0 || sid >= s->schema_count) { return -1; }
    return s->schemas[sid].add_prop(name, netudp::PropType::U8, flags, 1, 1);
}
int netudp_schema_add_u16(netudp_server_t* s, int sid, const char* name, uint16_t flags) {
    if (s == nullptr || sid < 0 || sid >= s->schema_count) { return -1; }
    return s->schemas[sid].add_prop(name, netudp::PropType::U16, flags, 2, 2);
}
int netudp_schema_add_i32(netudp_server_t* s, int sid, const char* name, uint16_t flags) {
    if (s == nullptr || sid < 0 || sid >= s->schema_count) { return -1; }
    return s->schemas[sid].add_prop(name, netudp::PropType::I32, flags, 4, 4);
}
int netudp_schema_add_f32(netudp_server_t* s, int sid, const char* name, uint16_t flags) {
    if (s == nullptr || sid < 0 || sid >= s->schema_count) { return -1; }
    return s->schemas[sid].add_prop(name, netudp::PropType::F32, flags, 4, 2);
}
int netudp_schema_add_vec3(netudp_server_t* s, int sid, const char* name, uint16_t flags) {
    if (s == nullptr || sid < 0 || sid >= s->schema_count) { return -1; }
    return s->schemas[sid].add_prop(name, netudp::PropType::VEC3, flags, 12, 4);
}
int netudp_schema_add_quat(netudp_server_t* s, int sid, const char* name, uint16_t flags) {
    if (s == nullptr || sid < 0 || sid >= s->schema_count) { return -1; }
    return s->schemas[sid].add_prop(name, netudp::PropType::QUAT, flags, 16, 4);
}
int netudp_schema_add_blob(netudp_server_t* s, int sid, const char* name, int max_bytes, uint16_t flags) {
    if (s == nullptr || sid < 0 || sid >= s->schema_count || max_bytes <= 0) { return -1; }
    return s->schemas[sid].add_prop(name, netudp::PropType::BLOB, flags, max_bytes, max_bytes, max_bytes);
}

uint16_t netudp_entity_create(netudp_server_t* server, int schema_id) {
    if (server == nullptr || server->entities == nullptr) { return 0; }
    if (schema_id < 0 || schema_id >= server->schema_count) { return 0; }
    if (server->schemas[schema_id].schema_id < 0) { return 0; }

    /* Find free entity slot */
    for (int i = 0; i < netudp_server::kMaxEntities; ++i) {
        if (!server->entities[i].active) {
            netudp::Entity& e = server->entities[i];
            e.reset();
            e.active = true;
            e.entity_id = server->next_entity_id++;
            if (server->next_entity_id == 0) { server->next_entity_id = 1; } /* Skip 0 */
            e.schema_id = schema_id;
            e.schema = &server->schemas[schema_id];
            return e.entity_id;
        }
    }
    return 0; /* No free slots */
}

void netudp_entity_destroy(netudp_server_t* server, uint16_t entity_id) {
    netudp::Entity* e = find_entity(server, entity_id);
    if (e != nullptr) { e->reset(); }
}

void netudp_entity_set_group(netudp_server_t* server, uint16_t entity_id, int group_id) {
    netudp::Entity* e = find_entity(server, entity_id);
    if (e != nullptr) { e->group_id = group_id; }
}

void netudp_entity_set_owner(netudp_server_t* server, uint16_t entity_id, int client_index) {
    netudp::Entity* e = find_entity(server, entity_id);
    if (e != nullptr) { e->owner_client = client_index; }
}

int netudp_entity_set_u8(netudp_server_t* s, uint16_t eid, int pi, uint8_t val) {
    netudp::Entity* e = find_entity(s, eid);
    return (e != nullptr && e->set_u8(pi, val)) ? NETUDP_OK : NETUDP_ERROR_INVALID_PARAM;
}
int netudp_entity_set_u16(netudp_server_t* s, uint16_t eid, int pi, uint16_t val) {
    netudp::Entity* e = find_entity(s, eid);
    return (e != nullptr && e->set_u16(pi, val)) ? NETUDP_OK : NETUDP_ERROR_INVALID_PARAM;
}
int netudp_entity_set_i32(netudp_server_t* s, uint16_t eid, int pi, int32_t val) {
    netudp::Entity* e = find_entity(s, eid);
    return (e != nullptr && e->set_i32(pi, val)) ? NETUDP_OK : NETUDP_ERROR_INVALID_PARAM;
}
int netudp_entity_set_f32(netudp_server_t* s, uint16_t eid, int pi, float val) {
    netudp::Entity* e = find_entity(s, eid);
    return (e != nullptr && e->set_f32(pi, val)) ? NETUDP_OK : NETUDP_ERROR_INVALID_PARAM;
}
int netudp_entity_set_vec3(netudp_server_t* s, uint16_t eid, int pi, const float v[3]) {
    netudp::Entity* e = find_entity(s, eid);
    return (e != nullptr && e->set_vec3(pi, v)) ? NETUDP_OK : NETUDP_ERROR_INVALID_PARAM;
}
int netudp_entity_set_quat(netudp_server_t* s, uint16_t eid, int pi, const float q[4]) {
    netudp::Entity* e = find_entity(s, eid);
    return (e != nullptr && e->set_quat(pi, q)) ? NETUDP_OK : NETUDP_ERROR_INVALID_PARAM;
}
int netudp_entity_set_blob(netudp_server_t* s, uint16_t eid, int pi, const void* data, int len) {
    netudp::Entity* e = find_entity(s, eid);
    return (e != nullptr && e->set_blob(pi, data, len)) ? NETUDP_OK : NETUDP_ERROR_INVALID_PARAM;
}

void netudp_entity_set_priority(netudp_server_t* server, uint16_t entity_id, uint8_t priority) {
    netudp::Entity* e = find_entity(server, entity_id);
    if (e != nullptr) { e->priority = priority; }
}

void netudp_entity_set_max_rate(netudp_server_t* server, uint16_t entity_id, float hz) {
    netudp::Entity* e = find_entity(server, entity_id);
    if (e != nullptr) { e->max_rate_hz = hz; }
}

void netudp_server_replicate(netudp_server_t* server) {
    NETUDP_ZONE("srv::replicate");
    if (server == nullptr || !server->running || server->entities == nullptr) {
        return;
    }

    uint8_t wire_buf[netudp::kEntityMaxValueSize + 32];

    double now = server->current_time;

    for (int ei = 0; ei < netudp_server::kMaxEntities; ++ei) {
        netudp::Entity& ent = server->entities[ei];
        if (!ent.active || ent.schema == nullptr) { continue; }
        if (ent.dirty_mask == 0 && !ent.needs_initial) { continue; }
        if (ent.group_id < 0 || ent.group_id >= server->max_groups) { continue; }

        /* Phase 43: rate limiting — check if enough time has passed since last replicate */
        if (!ent.needs_initial && ent.max_rate_hz > 0.0f) {
            double interval = 1.0 / static_cast<double>(ent.max_rate_hz);
            double elapsed = now - ent.last_replicate_time;
            if (elapsed < interval) {
                /* Starvation prevention: force update if too long since last send */
                if (elapsed < static_cast<double>(ent.min_update_interval)) {
                    continue; /* Rate limited — hold dirty bits for next tick */
                }
            }
        }

        const netudp::Group& grp = server->groups[ent.group_id];
        if (!grp.active || grp.member_count == 0) {
            ent.dirty_mask = 0;
            ent.needs_initial = false;
            continue;
        }

        /* Determine channel: reliable or unreliable based on REP_RELIABLE flag */
        bool any_reliable = false;
        for (int pi = 0; pi < ent.schema->prop_count; ++pi) {
            if ((ent.dirty_mask & (1ULL << pi)) != 0 &&
                (ent.schema->props[pi].rep_flags & netudp::REP_RELIABLE) != 0) {
                any_reliable = true;
                break;
            }
        }

        /* Find appropriate channel (0=unreliable, scan for first reliable if needed) */
        int channel = 0;
        if (any_reliable) {
            for (int ci = 0; ci < server->connections[grp.members[0]].num_channels; ++ci) {
                uint8_t ct = server->connections[grp.members[0]].ch(ci).type();
                if (ct == NETUDP_CHANNEL_RELIABLE_ORDERED || ct == NETUDP_CHANNEL_RELIABLE_UNORDERED) {
                    channel = ci;
                    break;
                }
            }
        }

        /* Serialize and send per member (filtering varies per client) */
        for (int mi = 0; mi < grp.member_count; ++mi) {
            int client = grp.members[mi];
            bool is_initial = ent.needs_initial; /* Simplified: initial for all on first replicate */

            int wire_len = netudp::replicate_serialize(ent, wire_buf, static_cast<int>(sizeof(wire_buf)),
                                                        client, is_initial);
            if (wire_len <= 0) { continue; }

            if (!any_reliable) {
                /* Use state overwrite for unreliable replication */
                netudp_server_send_state(server, client, channel, ent.entity_id, wire_buf, wire_len);
            } else {
                netudp_server_send(server, client, channel, wire_buf, wire_len, 0);
            }
        }

        ent.dirty_mask = 0;
        ent.needs_initial = false;
        ent.last_replicate_time = now;
    }
}

void netudp_server_set_packet_handler(netudp_server_t* server, uint16_t packet_type,
                                      netudp_packet_handler_fn fn, void* ctx) {
    if (server == nullptr || packet_type > 255) {
        return;
    }
    server->packet_handlers[packet_type] = fn;
    server->packet_handler_ctx[packet_type] = ctx;
}

} /* extern "C" */

/* ======================================================================
 * Internal: Connection request handling
 * ====================================================================== */

namespace netudp {

void server_handle_connection_request(netudp_server* server,
    const netudp_address_t* from, const uint8_t* packet, int packet_len) {
    NETUDP_ZONE("srv::conn_request");
    if (packet_len != 1078) {
        NLOG_WARN("[netudp] conn_request: bad packet length %d (expected 1078)", packet_len);
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
        NLOG_WARN("[netudp] conn_request: token AEAD decrypt failed (bad key or corrupt token)");
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
    uint64_t fp_key = 0;
    std::memcpy(&fp_key, fp.hash, 8);

    /* O(1) fingerprint anti-replay check */
    auto* existing = server->fingerprint_map.find(fp_key);
    if (existing != nullptr) {
        if (!netudp_address_equal(&existing->address, from)) {
            return; /* Same token used from different address — replay attack */
        }
        /* Same address reusing same token — allow (reconnect) */
    }

    /* Insert/update fingerprint entry */
    netudp_server::FingerprintValue fp_val;
    fp_val.address = *from;
    fp_val.expire_time = expire_ts;
    server->fingerprint_map.insert(fp_key, fp_val);

    /* O(1) slot allocation from free stack */
    if (server->free_count <= 0) {
        return; /* No free slots */
    }
    int slot = server->free_slots[--server->free_count];

    /* Establish connection with full subsystem init */
    Connection& conn = server->connections[slot];
    conn.active = true;
    conn.address = *from;
    conn.slot_id = static_cast<uint16_t>(slot);

    /* Cache pre-built sockaddr — eliminates per-send memset(128) + field copy (phase 38) */
    netudp::address_to_sockaddr(from, conn.cached_sa, &conn.cached_sa_len);

    /* Register in O(1) address→slot map */
    server->address_to_slot.insert(*from, slot);

    /* Add to active connection list */
    server->slot_to_active[slot] = server->active_count;
    server->active_slots[server->active_count++] = slot;
    conn.client_id = priv.client_id;
    std::memcpy(conn.user_data, priv.user_data, 256);
    std::memcpy(conn.key_epoch.tx_key, priv.server_to_client_key, 32);
    std::memcpy(conn.key_epoch.rx_key, priv.client_to_server_key, 32);
    conn.key_epoch.tx_nonce_counter = 0;
    conn.key_epoch.replay.reset();
    conn.timeout_seconds = priv.timeout_seconds;

    /* Init ALL subsystems */
    conn.init_subsystems(server->config.channels, server->config.num_channels, server->current_time);

    NLOG_INFO("[netudp] client %llu connected (slot=%d)",
                    (unsigned long long)priv.client_id, slot);

    if (server->config.on_connect != nullptr) {
        server->config.on_connect(server->config.callback_context,
                                   slot, priv.client_id, priv.user_data);
    }

    /* Send connection-accepted keepalive with slot_id + max_clients (phase 36).
     * The client reads this to learn its assigned slot_id for the wire header. */
    {
        Connection& c = server->connections[slot];
        double now = server->current_time;

        /* Payload: slot_id(u16) + max_clients(i32) + AckFields(8) = 14 bytes */
        uint8_t payload[14];
        uint16_t sid = static_cast<uint16_t>(slot);
        std::memcpy(payload, &sid, 2);
        int32_t mc = static_cast<int32_t>(server->max_clients);
        std::memcpy(payload + 2, &mc, 4);
        AckFields ack = c.packet_tracker.build_ack_fields(now);
        write_ack_fields(ack, payload + 6);

        c.packet_tracker.send_packet(now);

        uint8_t prefix = 0x15; /* KEEPALIVE */
        uint8_t ct[64];
        int ct_len = crypto::packet_encrypt(&c.key_epoch, server->protocol_id, prefix,
                                             payload, 14, ct);
        if (ct_len > 0) {
            server->send_buf[0] = prefix;
            std::memcpy(server->send_buf + 1, ct, static_cast<size_t>(ct_len));
            server_send_packet(server, &c.address, c.cached_sa, c.cached_sa_len,
                               server->send_buf, 1 + ct_len);
            c.last_send_time = now;
            c.last_keepalive_time = now;
        }
    }
}

/* ======================================================================
 * Internal: Handle data packet from established connection
 * ====================================================================== */

void server_handle_data_packet(netudp_server* server, int slot,
    const uint8_t* packet, int packet_len) {
    NETUDP_ZONE("srv::data_packet");
    Connection& conn = server->connections[slot];
    if (packet_len < 4) {  /* prefix(1) + slot_id(2) + at least 1 byte ciphertext */
        return;
    }

    uint8_t prefix = packet[0];

    /* Phase 36: header is now prefix(1) + slot_id(2) = 3 bytes */
    int header_len = 3;

    /* Nonce counter: next expected from this connection's replay window.
     * Fix: when most_recent=0 and nonce 0 was never received, the first
     * packet from this peer uses nonce 0 (not 1). */
    uint64_t expected_nonce = conn.key_epoch.replay.most_recent + 1;
    if (conn.key_epoch.replay.most_recent == 0 &&
        conn.key_epoch.replay.received[0] == crypto::ReplayProtection::EMPTY_SLOT) {
        expected_nonce = 0;
    }

    uint8_t plaintext[NETUDP_MAX_PACKET_ON_WIRE];
    int pt_len = crypto::packet_decrypt(
        &conn.key_epoch, server->protocol_id, prefix,
        expected_nonce,
        packet + header_len, packet_len - header_len, plaintext
    );

    if (pt_len < 0) {
        /* Grace window: try old_rx_key for 256 packets after peer rekeyed */
        pt_len = crypto::packet_decrypt_grace(
            &conn.key_epoch, server->protocol_id, prefix,
            expected_nonce,
            packet + header_len, packet_len - header_len, plaintext
        );
        if (pt_len < 0) {
            conn.stats.decrypt_failures++;
            server->ddos.on_bad_packet();
            NLOG_DEBUG("[netudp] data_packet: decrypt failed (slot=%d, failures=%llu)",
                       slot, (unsigned long long)conn.stats.decrypt_failures);
            return;
        }
    }

    /* REKEY packet: peer has rekeyed — derive new keys on our side */
    if (prefix == crypto::PACKET_PREFIX_DATA_REKEY) {
        crypto::on_receive_rekey(conn.key_epoch, server->current_time);
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
            conn.delivered().push_back(dmsg);
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
                            DeliveredMessage dmsg;
                            int copy_len = std::min(len, static_cast<int>(NETUDP_MTU));
                            std::memcpy(dmsg.data, data, static_cast<size_t>(copy_len));
                            dmsg.size = copy_len;
                            dmsg.channel = ch;
                            dmsg.sequence = seq;
                            dmsg.valid = true;
                            conn.delivered().push_back(dmsg);
                            conn.stats.messages_received++;
                        });
                }
            } else if (ch_type == NETUDP_CHANNEL_RELIABLE_UNORDERED) {
                if (!conn.rs(ch).is_received_unordered(msg_seq)) {
                    conn.rs(ch).mark_received_unordered(msg_seq);
                    DeliveredMessage dmsg;
                    int copy_len = std::min(static_cast<int>(msg_len), static_cast<int>(NETUDP_MTU));
                    std::memcpy(dmsg.data, plaintext + pos, static_cast<size_t>(copy_len));
                    dmsg.size = copy_len;
                    dmsg.channel = ch;
                    dmsg.sequence = msg_seq;
                    dmsg.valid = true;
                    conn.delivered().push_back(dmsg);
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
            const uint8_t* complete = conn.frag().on_fragment_received(
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
                conn.delivered().push_back(dmsg);
                conn.stats.fragments_received++;
            }
            conn.stats.fragments_received++;
            pos = pt_len; /* Fragment consumes rest */

        } else if (frame_type == wire::FRAME_DISCONNECT) {
            int reason = (pos < pt_len) ? plaintext[pos] : 0;
            NLOG_INFO("[netudp] client %llu disconnected (slot=%d, reason=%d)",
                            (unsigned long long)conn.client_id, slot, reason);
            if (server->config.on_disconnect != nullptr) {
                server->config.on_disconnect(server->config.callback_context, slot, reason);
            }
            server->address_to_slot.remove(conn.address);
            server_deactivate_slot(server, slot);
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — coalescing loop with multiple frame types
void server_send_pending(netudp_server* server, int slot) {
    NETUDP_ZONE("srv::send_pending");
    Connection& conn = server->connections[slot];
    double now = server->current_time;

    /* Check bandwidth */
    if (!conn.budget.can_send()) {
        return;
    }

    /* Reserve space for MAC (16 bytes) in payload budget */
    static constexpr int kPayloadBudget = NETUDP_MTU;

    /* Payload buffer: [AckFields 8][frame1][frame2]...[frameN] */
    uint8_t payload[NETUDP_MTU];
    int payload_pos = 0;
    int frames_packed = 0;

    /* Write AckFields once for this coalesced packet */
    AckFields ack = conn.packet_tracker.build_ack_fields(now);
    payload_pos += write_ack_fields(ack, payload + payload_pos);

    /* Iterate channels, packing frames until MTU is full or queues are empty */
    int ch_idx = ChannelScheduler::next_channel_fast(conn.cdata->channels, conn.num_channels, now, conn.pending_mask);
    while (ch_idx >= 0) {
        NETUDP_ZONE("srv::coalesce");

        QueuedMessage qmsg;
        if (!conn.ch(ch_idx).dequeue_send(&qmsg)) {
            ch_idx = ChannelScheduler::next_channel_fast(conn.cdata->channels, conn.num_channels, now, conn.pending_mask);
            continue;
        }

        /* Calculate frame overhead */
        uint8_t ch_type = conn.ch(ch_idx).type();
        bool is_reliable = (ch_type == NETUDP_CHANNEL_RELIABLE_ORDERED ||
                            ch_type == NETUDP_CHANNEL_RELIABLE_UNORDERED);
        int frame_overhead = is_reliable ? 6 : 4;
        int needed = frame_overhead + qmsg.size;
        int remaining = kPayloadBudget - payload_pos;

        /* If this frame doesn't fit and we already have frames, flush first */
        if (needed > remaining && frames_packed > 0) {
            /* Flush current coalesced packet */
            conn.packet_tracker.send_packet(now);

            if (crypto::should_rekey(conn.key_epoch, now) && !conn.key_epoch.rekey_pending) {
                crypto::prepare_rekey(conn.key_epoch);
            }

            uint8_t prefix = conn.key_epoch.rekey_pending
                             ? crypto::PACKET_PREFIX_DATA_REKEY
                             : static_cast<uint8_t>(0x14);
            uint8_t ct[NETUDP_MAX_PACKET_ON_WIRE];
            int ct_len = crypto::packet_encrypt(&conn.key_epoch, server->protocol_id, prefix,
                                                 payload, payload_pos, ct);
            if (ct_len < 0) {
                NLOG_ERROR("[netudp] send_pending: encryption failed (slot=%d)", slot);
                break;
            }

            if (conn.key_epoch.rekey_pending) {
                crypto::activate_rekey(conn.key_epoch, now);
            }

            server->send_buf[0] = prefix;
            std::memcpy(server->send_buf + 1, ct, static_cast<size_t>(ct_len));
            int total = 1 + ct_len;
            server_send_packet(server, &conn.address, conn.cached_sa, conn.cached_sa_len, server->send_buf, total);
            conn.last_send_time = now;
            conn.stats.on_packet_sent(total);
            conn.stats.frames_coalesced += static_cast<uint32_t>(frames_packed);
            conn.budget.consume(total);

            if (!conn.budget.can_send()) {
                return;
            }

            /* Start new packet */
            payload_pos = 0;
            ack = conn.packet_tracker.build_ack_fields(now);
            payload_pos += write_ack_fields(ack, payload + payload_pos);
            frames_packed = 0;
            remaining = kPayloadBudget - payload_pos;
        }

        /* Write frame into payload */
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
            NLOG_ERROR("[netudp] send_pending: frame encode failed (slot=%d, ch=%d)", slot, ch_idx);
            break;
        }

        payload_pos += frame_len;
        frames_packed++;

        ch_idx = ChannelScheduler::next_channel_fast(conn.cdata->channels, conn.num_channels, now, conn.pending_mask);
    }

    /* Flush remaining frames */
    if (frames_packed > 0) {
        conn.packet_tracker.send_packet(now);

        if (crypto::should_rekey(conn.key_epoch, now) && !conn.key_epoch.rekey_pending) {
            crypto::prepare_rekey(conn.key_epoch);
        }

        uint8_t prefix = conn.key_epoch.rekey_pending
                         ? crypto::PACKET_PREFIX_DATA_REKEY
                         : static_cast<uint8_t>(0x14);
        uint8_t ct[NETUDP_MAX_PACKET_ON_WIRE];
        int ct_len = crypto::packet_encrypt(&conn.key_epoch, server->protocol_id, prefix,
                                             payload, payload_pos, ct);
        if (ct_len >= 0) {
            if (conn.key_epoch.rekey_pending) {
                crypto::activate_rekey(conn.key_epoch, now);
            }

            server->send_buf[0] = prefix;
            std::memcpy(server->send_buf + 1, ct, static_cast<size_t>(ct_len));
            int total = 1 + ct_len;
            server_send_packet(server, &conn.address, conn.cached_sa, conn.cached_sa_len, server->send_buf, total);
            conn.last_send_time = now;
            conn.stats.on_packet_sent(total);
            conn.stats.frames_coalesced += static_cast<uint32_t>(frames_packed);
            conn.budget.consume(total);
        } else {
            NLOG_ERROR("[netudp] send_pending: final encryption failed (slot=%d)", slot);
        }
    }
}

/* ======================================================================
 * Internal: Send keepalive packet
 * ====================================================================== */

void server_send_keepalive(netudp_server* server, int slot) {
    NETUDP_ZONE("srv::keepalive");
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
    server_send_packet(server, &conn.address, conn.cached_sa, conn.cached_sa_len, server->send_buf, 1 + ct_len);
    conn.last_send_time = now;
    conn.last_keepalive_time = now;
}

} // namespace netudp
