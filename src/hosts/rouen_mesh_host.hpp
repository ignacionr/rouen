#pragma once

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <mongoose.h>
#include "../helpers/glaze_include.hpp"

namespace rouen::mesh {

// 1. Frame Type Identifier (2 bytes uint16_t Big-Endian)
enum class frame_type : uint16_t {
    HEARTBEAT_PING      = 0x0001,
    HEARTBEAT_PONG      = 0x0002,

    HTTP_REQUEST        = 0x0010,
    HTTP_RESPONSE       = 0x0011,
    HTTP_RESPONSE_CHUNK = 0x0012,

    REGISTRY_SET        = 0x0020,
    REGISTRY_GET        = 0x0021,
    REGISTRY_RESP       = 0x0022,
    REGISTRY_LIST       = 0x0023,

    PAIRING_REQ         = 0x0030,
    PAIRING_RESP        = 0x0031,
    CLIENT_LIST_REQ     = 0x0032,
    CLIENT_LIST_RESP    = 0x0033,

    ROUTE_OPEN          = 0x0040,
    ROUTE_OPEN_ACK      = 0x0041,
    ROUTE_CLOSE         = 0x0042,
    ROUTE_DATA          = 0x0043,
    ROUTE_LIST_REQ      = 0x0044,
    ROUTE_LIST_RESP     = 0x0045,

    RAW_DATA            = 0x00FF
};

// 2. Bitwise Flags (2 bytes uint16_t Big-Endian)
namespace frame_flags {
    constexpr uint16_t NONE         = 0x0000;
    constexpr uint16_t ENCRYPTED    = 0x0001;
    constexpr uint16_t COMPRESSED   = 0x0002;
    constexpr uint16_t JSON_PAYLOAD = 0x0004;
    constexpr uint16_t STREAM_END   = 0x0008;
}

// 3. Binary 12-Byte Packed Frame Header
#pragma pack(push, 1)
struct frame_header {
    uint32_t length{0};    // Big-Endian
    uint16_t type{0};      // Big-Endian
    uint16_t flags{0};     // Big-Endian
    uint32_t route_id{0};  // Big-Endian
};
#pragma pack(pop)

static_assert(sizeof(frame_header) == 12, "frame_header must be exactly 12 bytes");

// 4. Parsed Mesh Frame
struct mesh_frame {
    frame_type type{frame_type::RAW_DATA};
    uint16_t flags{frame_flags::NONE};
    uint32_t route_id{0};
    std::string payload;

    [[nodiscard]] uint32_t payload_length() const noexcept {
        return static_cast<uint32_t>(payload.size());
    }

    [[nodiscard]] bool is_flag_set(uint16_t flag) const noexcept {
        return (flags & flag) != 0;
    }
};

// 5. Binary Frame Codec
class frame_codec {
public:
    static constexpr size_t HEADER_SIZE = sizeof(frame_header);
    static constexpr size_t MAX_PAYLOAD_SIZE = 16 * 1024 * 1024; // 16 MB limit

    static std::vector<uint8_t> encode(frame_type type, uint16_t flags, uint32_t route_id, std::string_view payload) {
        uint32_t len = static_cast<uint32_t>(payload.size());
        
        frame_header header{
            .length = htonl(len),
            .type = htons(static_cast<uint16_t>(type)),
            .flags = htons(flags),
            .route_id = htonl(route_id)
        };

        std::vector<uint8_t> buffer;
        buffer.reserve(HEADER_SIZE + payload.size());

        const auto* header_bytes = reinterpret_cast<const uint8_t*>(&header);
        buffer.insert(buffer.end(), header_bytes, header_bytes + HEADER_SIZE);
        buffer.insert(buffer.end(), payload.begin(), payload.end());
        return buffer;
    }

    static std::vector<uint8_t> encode(const mesh_frame& frame) {
        return encode(frame.type, frame.flags, frame.route_id, frame.payload);
    }

    static std::optional<mesh_frame> decode(const uint8_t* data, size_t size) {
        if (size < HEADER_SIZE) {
            return std::nullopt;
        }

        const auto* header = reinterpret_cast<const frame_header*>(data);
        uint32_t payload_len = ntohl(header->length);
        uint16_t raw_type = ntohs(header->type);
        uint16_t flags = ntohs(header->flags);
        uint32_t route_id = ntohl(header->route_id);

        if (payload_len > MAX_PAYLOAD_SIZE) {
            return std::nullopt;
        }

        if (size < HEADER_SIZE + payload_len) {
            return std::nullopt;
        }

        mesh_frame frame;
        frame.type = static_cast<frame_type>(raw_type);
        frame.flags = flags;
        frame.route_id = route_id;
        frame.payload.assign(reinterpret_cast<const char*>(data + HEADER_SIZE), payload_len);

        return frame;
    }

    static std::optional<mesh_frame> decode(std::string_view binary_data) {
        return decode(reinterpret_cast<const uint8_t*>(binary_data.data()), binary_data.size());
    }
};

/**
 * Rouen Mesh Transparent Port Proxying & Relay Tunneling Architecture
 * ===================================================================
 * Rouen Mesh offers transparent TCP and HTTP proxying by binding local listening
 * sockets that expose remote mesh node resources, operating similarly to SSH local
 * port forwarding (`ssh -L local_port:target_host:target_port`).
 *
 * Virtual routes bind local TCP listeners on `127.0.0.1:local_port`. Any local client
 * (HTTP client, curl, AI model host client, browser, or raw socket app) connecting to
 * `127.0.0.1:local_port` is transparently tunneled across the Mesh WebSocket daemon
 * connection using 12-byte binary multiplexed frames:
 *   - ROUTE_OPEN      (0x0040): Opens virtual stream route to remote target node & port
 *   - ROUTE_OPEN_ACK  (0x0041): Acknowledges route stream readiness
 *   - ROUTE_DATA      (0x0043): Carries raw TCP stream payloads between endpoints
 *   - ROUTE_CLOSE     (0x0042): Teardowns route stream session
 */

// Mesh Service Information DTO
struct mesh_service_info {
    std::string client_id;
    std::string service;
    std::string protocol{"http"};
    uint16_t target_port{8081};
    std::vector<std::string> capabilities;
    bool auth_required{false};
};

// Mesh Client Node Discovery DTO
struct mesh_client_dto {
    std::string client_id;
    std::string ip_address;
    std::string user_agent;
    uint64_t uptime_seconds{0};
    uint64_t last_ping_ago_seconds{0};
    uint64_t requests_tunneled{0};
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
};

// Virtual Route Information DTO
struct virtual_route_info {
    uint32_t route_id{0};
    std::string source_client_id;
    std::string target_client_id;
    std::string target_host{"127.0.0.1"};
    uint16_t target_port{0};
    uint16_t local_port{0};
    uint64_t bytes_transferred{0};
    std::string status{"active"}; // "active", "listening", "closed"
    std::string local_url;
};

// Virtual Route ACK DTO
struct route_open_ack_payload {
    uint32_t route_id{0};
    std::string status{"ok"};
    std::string reason;
};

struct route_open_request {
    std::string target_client_id;
    uint16_t target_port{0};
    uint16_t local_port{0};
};

struct route_listener_ctx {
    uint32_t route_id{0};
    std::string target_client_id;
    uint16_t target_port{0};
    uint16_t local_port{0};
    struct mg_connection* listener_conn{nullptr};
};

struct route_stream_ctx {
    uint32_t route_id{0};
    std::string source_client_id;
    std::string target_client_id;
    uint16_t target_port{0};
    uint16_t local_port{0};
    struct mg_connection* local_conn{nullptr};   // Outbound local socket connection
    struct mg_connection* target_conn{nullptr};  // Inbound remote target connection
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    bool is_inbound{false};
};

} // namespace rouen::mesh

namespace rouen::hosts {

struct mesh_host_config {
    bool enabled{false};
    std::string server_url{"wss://rouen.inz.dev/ws/connect"};
    std::string client_id{"rouen-node"};
    bool is_paired{false};
    std::string public_key;
    std::string private_key;
    uint16_t local_api_port{8081};
    uint16_t local_llm_port{11434};
    std::chrono::seconds reconnect_interval{5};
    std::chrono::seconds ping_interval{15};
};

class rouen_mesh_host {
public:
    using config = mesh_host_config;

    static rouen_mesh_host& instance();

    rouen_mesh_host(const rouen_mesh_host&) = delete;
    rouen_mesh_host& operator=(const rouen_mesh_host&) = delete;

    bool initialize();
    bool initialize(const config& cfg);
    bool start();
    void stop();

    [[nodiscard]] bool is_connected() const noexcept;
    [[nodiscard]] bool is_paired() const noexcept;
    [[nodiscard]] std::string get_status_message() const;
    [[nodiscard]] config get_config() const;
    void set_config(const config& cfg);

    // Dynamic hostname client ID helper
    static std::string generate_default_client_id();

    // Keypair generation helper
    static void generate_keypair(std::string& out_public_hex, std::string& out_private_hex);

    // Route Management
    bool open_virtual_route(const std::string& target_client_id, uint16_t target_port, std::string& out_error, uint16_t local_port = 0);
    bool close_virtual_route(uint32_t route_id);
    [[nodiscard]] std::vector<mesh::virtual_route_info> get_active_routes() const;

    // Discovery APIs
    void refresh_connected_clients();
    [[nodiscard]] std::vector<mesh::mesh_client_dto> get_connected_clients() const;
    void refresh_server_routes();

    // Traffic Metrics
    [[nodiscard]] uint64_t get_total_requests() const noexcept;
    [[nodiscard]] uint64_t get_total_bytes_sent() const noexcept;
    [[nodiscard]] uint64_t get_total_bytes_received() const noexcept;
    [[nodiscard]] uint32_t get_ping_ms() const noexcept;

    // Service Discovery & Registration
    void register_service(const mesh::mesh_service_info& info);
    void unregister_service(const std::string& service_name);
    void refresh_peer_services();
    [[nodiscard]] std::vector<mesh::mesh_service_info> get_peer_services() const;

    bool send_pairing_request(const std::string& server_http_base, const std::string& pairing_code, std::string& out_error);

    // Frame processing pipeline
    void handle_incoming_frame(const mesh::mesh_frame& frame);

    // WebSocket connection event handlers
    void on_ws_connected(struct mg_connection* c);
    void on_ws_disconnected(const std::string& reason = "Disconnected from rouen-service");

    // Frame transmission helper
    void send_frame_over_ws(mesh::frame_type type, uint16_t flags, uint32_t route_id, std::string_view payload);

    // Handshake signature helper
    [[nodiscard]] std::string generate_handshake_signature(const std::string& client_id, uint64_t timestamp_ms) const;

    // Reset state for testing
    void clear();

    // Event handlers for transparent proxy listeners and streams
    void handle_route_listener_event(struct mg_connection* c, int ev, void* ev_data, mesh::route_listener_ctx* ctx);
    void handle_route_stream_event(struct mg_connection* c, int ev, void* ev_data, mesh::route_stream_ctx* ctx);
    void handle_inbound_target_event(struct mg_connection* c, int ev, void* ev_data, mesh::route_stream_ctx* ctx);

private:
    rouen_mesh_host() = default;
    ~rouen_mesh_host();

    void worker_loop();
    void process_pending_route_requests(struct mg_mgr* mgr);

    mutable std::recursive_mutex mutex_;
    config config_{};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};

    std::string status_message_{"Disconnected"};

    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> total_bytes_sent_{0};
    std::atomic<uint64_t> total_bytes_received_{0};
    std::atomic<uint32_t> ping_ms_{0};
    std::atomic<uint32_t> next_route_id_{1001};

    struct mg_connection* active_ws_conn_{nullptr};

    std::unordered_map<std::string, mesh::mesh_service_info> local_services_;
    std::unordered_map<std::string, mesh::mesh_service_info> peer_services_;
    std::unordered_map<uint32_t, mesh::virtual_route_info> active_routes_;
    std::vector<mesh::mesh_client_dto> connected_clients_;

    std::vector<mesh::route_open_request> pending_route_requests_;
    std::unordered_map<uint32_t, std::shared_ptr<mesh::route_listener_ctx>> active_listeners_;
    std::unordered_map<uint32_t, std::shared_ptr<mesh::route_stream_ctx>> active_streams_;

    std::unique_ptr<std::thread> worker_thread_;
};

} // namespace rouen::hosts

