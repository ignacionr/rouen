#include "rouen_mesh_host.hpp"

#include <chrono>
#include <format>
#include <iomanip>
#include <iostream>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <io.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif
#include <glaze/glaze.hpp>
#include <mongoose.h>
#include "../helpers/config_service.hpp"
#include "../helpers/fetch.hpp"

namespace rouen::hosts {

static bool is_local_port_available(uint16_t port) {
#ifdef _WIN32
    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    char reuse = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    bool available = (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    ::closesocket(sock);
    return available;
#else
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int reuse = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    bool available = (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    ::close(sock);
    return available;
#endif
}

static uint16_t find_available_mesh_port(uint16_t start_port) {
    for (uint32_t port = start_port; port <= 65535; ++port) {
        if (is_local_port_available(static_cast<uint16_t>(port))) {
            return static_cast<uint16_t>(port);
        }
    }
    return start_port;
}

rouen_mesh_host& rouen_mesh_host::instance() {
    static rouen_mesh_host inst;
    if (!inst.initialized_.load()) {
        inst.initialize();
    }
    return inst;
}

rouen_mesh_host::~rouen_mesh_host() {
    stop();
}

std::string rouen_mesh_host::generate_default_client_id() {
    char hostname_buf[256] = {0};
    if (gethostname(hostname_buf, sizeof(hostname_buf)) == 0 && hostname_buf[0] != '\0') {
        std::string raw(hostname_buf);
        size_t dot_pos = raw.find('.');
        if (dot_pos != std::string::npos) {
            raw = raw.substr(0, dot_pos);
        }
        std::string cleaned;
        for (char c : raw) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                cleaned += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            } else if (c == '-' || c == '_') {
                cleaned += c;
            }
        }
        if (!cleaned.empty()) {
            if (cleaned.starts_with("rouen-")) {
                return cleaned;
            }
            return "rouen-" + cleaned;
        }
    }
    return "rouen-node";
}

void rouen_mesh_host::generate_keypair(std::string& out_public_hex, std::string& out_private_hex) {
    EVP_PKEY* pkey = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
    if (pkey) {
        size_t pub_len = 32, priv_len = 32;
        unsigned char pub_buf[32], priv_buf[32];
        if (EVP_PKEY_get_raw_public_key(pkey, pub_buf, &pub_len) == 1 &&
            EVP_PKEY_get_raw_private_key(pkey, priv_buf, &priv_len) == 1) {
            
            std::ostringstream pub_ss, priv_ss;
            for (size_t i = 0; i < pub_len; ++i) {
                pub_ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(pub_buf[i]);
            }
            for (size_t i = 0; i < priv_len; ++i) {
                priv_ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(priv_buf[i]);
            }
            out_public_hex = pub_ss.str();
            out_private_hex = priv_ss.str();
            EVP_PKEY_free(pkey);
            return;
        }
        EVP_PKEY_free(pkey);
    }

    // Dynamic cryptographic random byte generation fallback if OpenSSL ED25519 keygen fails
    unsigned char pub_buf[32], priv_buf[32];
    RAND_bytes(pub_buf, 32);
    RAND_bytes(priv_buf, 32);
    std::ostringstream pub_ss, priv_ss;
    for (int i = 0; i < 32; ++i) {
        pub_ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(pub_buf[i]);
        priv_ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(priv_buf[i]);
    }
    out_public_hex = pub_ss.str();
    out_private_hex = priv_ss.str();
}

bool rouen_mesh_host::initialize() {
    auto config_svc = helpers::ConfigService::instance();
    config cfg;
    cfg.server_url = config_svc->get_env("ROUEN_MESH_SERVER_URL");
    if (cfg.server_url.empty()) {
        cfg.server_url = "wss://rouen.inz.dev/ws/connect";
    }
    cfg.client_id = config_svc->get_env("ROUEN_MESH_CLIENT_ID");
    if (cfg.client_id.empty() || cfg.client_id == "rouen-macbook-pro") {
        cfg.client_id = generate_default_client_id();
    }
    cfg.public_key = config_svc->get_env("ROUEN_MESH_PUBLIC_KEY");
    cfg.private_key = config_svc->get_env("ROUEN_MESH_PRIVATE_KEY");
    cfg.is_paired = (config_svc->get_env("ROUEN_MESH_PAIRED") == "1");

    return initialize(cfg);
}

bool rouen_mesh_host::initialize(const config& cfg) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    config_ = cfg;

    if (config_.client_id.empty() || config_.client_id == "rouen-macbook-pro") {
        config_.client_id = generate_default_client_id();
    }

    if (config_.public_key.empty() || config_.private_key.empty()) {
        generate_keypair(config_.public_key, config_.private_key);
    }

    // Default register Rouen internal REST API
    mesh::mesh_service_info api_service{
        .client_id = config_.client_id,
        .service = "rest_api",
        .protocol = "http",
        .target_port = config_.local_api_port,
        .capabilities = {"adaptive_cards", "deck_control", "process_ui"},
        .auth_required = false
    };
    local_services_["rest_api"] = api_service;

    // Load persisted user-configured custom mesh services
    load_custom_services();

    config_.is_paired = true;
    status_message_ = "Ready to connect.";

    initialized_.store(true);
    return true;
}

bool rouen_mesh_host::start() {
    if (!initialized_.load()) {
        initialize();
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (running_.load()) {
        return true;
    }

    running_.store(true);
    connected_.store(false);
    status_message_ = "Connecting to " + config_.server_url + "...";

    worker_thread_ = std::make_unique<std::thread>(&rouen_mesh_host::worker_loop, this);
    return true;
}

void rouen_mesh_host::stop() {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        running_.store(false);
        connected_.store(false);
        status_message_ = "Disconnected";
        active_ws_conn_ = nullptr;
    }

    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
        worker_thread_.reset();
    }
}

static void mg_mesh_event_handler(struct mg_connection* c, int ev, void* ev_data) {
    if (!c || !c->fn_data) return;
    auto* host = static_cast<rouen_mesh_host*>(c->fn_data);

    if (ev == MG_EV_OPEN) {
        std::cout << "[MeshWS] MG_EV_OPEN: Connection object created." << std::endl;
    } else if (ev == MG_EV_CONNECT) {
        std::cout << "[MeshWS] MG_EV_CONNECT: TCP Socket connected." << std::endl;
    } else if (ev == MG_EV_WS_OPEN) {
        std::cout << "[MeshWS] MG_EV_WS_OPEN: WebSocket Handshake complete!" << std::endl;
        host->on_ws_connected(c);
    } else if (ev == MG_EV_WS_MSG) {
        auto* wm = static_cast<struct mg_ws_message*>(ev_data);
        if (wm && wm->data.buf && wm->data.len > 0) {
            std::string_view frame_bytes(wm->data.buf, wm->data.len);
            auto opt_frame = mesh::frame_codec::decode(frame_bytes);
            if (opt_frame) {
                host->handle_incoming_frame(*opt_frame);
            }
        }
    } else if (ev == MG_EV_ERROR) {
        const char* err_msg = static_cast<const char*>(ev_data);
        std::cout << "[MeshWS] MG_EV_ERROR: " << (err_msg ? err_msg : "unknown error") << std::endl;
        host->on_ws_disconnected(err_msg ? err_msg : "WebSocket error");
    } else if (ev == MG_EV_CLOSE) {
        std::cout << "[MeshWS] MG_EV_CLOSE triggered" << std::endl;
        host->on_ws_disconnected("Connection closed");
    }
}

static void mg_route_listener_handler(struct mg_connection* c, int ev, void* ev_data) {
    if (!c || !c->fn_data) return;
    auto* ctx = static_cast<mesh::route_listener_ctx*>(c->fn_data);
    auto& host = rouen_mesh_host::instance();
    host.handle_route_listener_event(c, ev, ev_data, ctx);
}

static void mg_route_stream_handler(struct mg_connection* c, int ev, void* ev_data) {
    if (!c || !c->fn_data) return;
    auto* ctx = static_cast<mesh::route_stream_ctx*>(c->fn_data);
    auto& host = rouen_mesh_host::instance();
    host.handle_route_stream_event(c, ev, ev_data, ctx);
}

static void mg_inbound_target_handler(struct mg_connection* c, int ev, void* ev_data) {
    if (!c || !c->fn_data) return;
    auto* ctx = static_cast<mesh::route_stream_ctx*>(c->fn_data);
    auto& host = rouen_mesh_host::instance();
    host.handle_inbound_target_event(c, ev, ev_data, ctx);
}

void rouen_mesh_host::on_ws_connected(struct mg_connection* c) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    active_ws_conn_ = c;
    connected_.store(true);
    status_message_ = "Connected to rouen-service";

    // Publish own local services and request fresh online clients and peer services
    publish_local_services();
    refresh_connected_clients();
    refresh_peer_services();
}

void rouen_mesh_host::on_ws_disconnected(const std::string& reason) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    connected_.store(false);
    active_ws_conn_ = nullptr;
    status_message_ = "Disconnected: " + reason;
}

void rouen_mesh_host::send_frame_over_ws(mesh::frame_type type, uint16_t flags, uint32_t route_id, std::string_view payload) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!active_ws_conn_) return;
    auto bytes = mesh::frame_codec::encode(type, flags, route_id, payload);
    mg_ws_send(active_ws_conn_, bytes.data(), bytes.size(), WEBSOCKET_OP_BINARY);
}

void rouen_mesh_host::handle_route_listener_event(struct mg_connection* c, int ev, void* ev_data, mesh::route_listener_ctx* ctx) {
    (void)ev_data;
    if (!ctx) return;

    if (ev == MG_EV_ACCEPT) {
        uint32_t stream_route_id = next_route_id_++;
        std::cout << "[MeshTunnel] Listener accepted TCP connection on local port " << ctx->local_port << " -> stream route #" << stream_route_id << std::endl;

        auto stream = std::make_shared<mesh::route_stream_ctx>();
        stream->route_id = stream_route_id;
        stream->source_client_id = config_.client_id;
        stream->target_client_id = ctx->target_client_id;
        stream->target_port = ctx->target_port;
        stream->local_port = ctx->local_port;
        stream->local_conn = c;
        stream->is_inbound = false;

        c->fn_data = stream.get();
        c->fn = mg_route_stream_handler;

        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            active_streams_[stream_route_id] = stream;
        }

        std::string open_payload = std::format(
            R"({{"target_client_id":"{}","target_port":{}}})",
            ctx->target_client_id, ctx->target_port
        );
        send_frame_over_ws(mesh::frame_type::ROUTE_OPEN, mesh::frame_flags::JSON_PAYLOAD, stream_route_id, open_payload);
    }
}

void rouen_mesh_host::handle_route_stream_event(struct mg_connection* c, int ev, void* ev_data, mesh::route_stream_ctx* ctx) {
    (void)ev_data;
    if (!ctx) return;

    if (c->is_draining && c->send.len == 0) {
        c->is_closing = 1;
    }

    if (ev == MG_EV_READ) {
        if (c->recv.buf && c->recv.len > 0) {
            std::string_view payload(reinterpret_cast<const char*>(c->recv.buf), c->recv.len);
            std::cout << "[MeshTunnel] Outbound client sent " << payload.size() << " bytes on route #" << ctx->route_id << std::endl;
            
            total_bytes_sent_ += payload.size();
            total_requests_++;
            ctx->bytes_sent += payload.size();

            send_frame_over_ws(mesh::frame_type::ROUTE_DATA, mesh::frame_flags::NONE, ctx->route_id, payload);

            mg_iobuf_del(&c->recv, 0, c->recv.len);
        }
    } else if (ev == MG_EV_CLOSE) {
        std::cout << "[MeshTunnel] Outbound client closed connection on route #" << ctx->route_id << std::endl;
        send_frame_over_ws(mesh::frame_type::ROUTE_CLOSE, mesh::frame_flags::NONE, ctx->route_id, "");
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            ctx->local_conn = nullptr;
            if (ctx->target_conn == nullptr) {
                active_streams_.erase(ctx->route_id);
            }
        }
    }
}

void rouen_mesh_host::handle_inbound_target_event(struct mg_connection* c, int ev, void* ev_data, mesh::route_stream_ctx* ctx) {
    (void)ev_data;
    if (!ctx) return;

    if (c->is_draining && c->send.len == 0) {
        c->is_closing = 1;
    }

    if (ev == MG_EV_CONNECT) {
        std::cout << "[MeshTunnel] Inbound target TCP connected to target port " << ctx->target_port << " for route #" << ctx->route_id << std::endl;
        std::string ack = R"({"status":"ok","reason":"Connected to local service target"})";
        send_frame_over_ws(mesh::frame_type::ROUTE_OPEN_ACK, mesh::frame_flags::JSON_PAYLOAD, ctx->route_id, ack);
    } else if (ev == MG_EV_READ) {
        if (c->recv.buf && c->recv.len > 0) {
            std::string_view payload(reinterpret_cast<const char*>(c->recv.buf), c->recv.len);
            std::cout << "[MeshTunnel] Inbound target service read " << payload.size() << " bytes on route #" << ctx->route_id << std::endl;

            total_bytes_received_ += payload.size();
            ctx->bytes_received += payload.size();

            send_frame_over_ws(mesh::frame_type::ROUTE_DATA, mesh::frame_flags::INBOUND_DIR, ctx->route_id, payload);

            mg_iobuf_del(&c->recv, 0, c->recv.len);
        }
    } else if (ev == MG_EV_CLOSE) {
        std::cout << "[MeshTunnel] Inbound target service closed connection on route #" << ctx->route_id << std::endl;
        send_frame_over_ws(mesh::frame_type::ROUTE_CLOSE, mesh::frame_flags::NONE, ctx->route_id, "");
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            ctx->target_conn = nullptr;
            if (ctx->local_conn == nullptr) {
                active_streams_.erase(ctx->route_id);
            }
        }
    }
}

void rouen_mesh_host::process_pending_route_requests(struct mg_mgr* mgr) {
    std::vector<mesh::route_open_request> reqs;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (pending_route_requests_.empty()) return;
        reqs.swap(pending_route_requests_);
    }

    for (const auto& req : reqs) {
        uint32_t route_id = req.route_id;

        auto listener_ctx = std::make_shared<mesh::route_listener_ctx>();
        listener_ctx->route_id = route_id;
        listener_ctx->target_client_id = req.target_client_id;
        listener_ctx->target_port = req.target_port;
        listener_ctx->local_port = req.local_port;

        std::string listen_url = std::format("tcp://127.0.0.1:{}", req.local_port);
        struct mg_connection* listener = mg_listen(mgr, listen_url.c_str(), mg_route_listener_handler, listener_ctx.get());

        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (listener) {
            listener_ctx->listener_conn = listener;
            active_listeners_[route_id] = listener_ctx;

            auto it = active_routes_.find(route_id);
            if (it != active_routes_.end()) {
                it->second.status = "listening";
            } else {
                std::string local_url = std::format("http://127.0.0.1:{}", req.local_port);
                mesh::virtual_route_info route_info{
                    .route_id = route_id,
                    .source_client_id = config_.client_id,
                    .target_client_id = req.target_client_id,
                    .target_host = "127.0.0.1",
                    .target_port = req.target_port,
                    .local_port = req.local_port,
                    .bytes_transferred = 0,
                    .status = "listening",
                    .local_url = local_url
                };
                active_routes_[route_id] = route_info;
            }
            std::cout << "[Mesh] Transparent TCP listener active on " << listen_url << " -> " << req.target_client_id << ":" << req.target_port << std::endl;
        } else {
            auto it = active_routes_.find(route_id);
            if (it != active_routes_.end()) {
                it->second.status = "failed (port in use)";
            }
            std::cerr << "[Mesh] Failed to bind transparent TCP listener on " << listen_url << std::endl;
        }
    }
}

void rouen_mesh_host::worker_loop() {
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        current_mgr_ = &mgr;
    }

    struct mg_connection* active_conn = nullptr;
    auto last_connect_try = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    while (running_.load()) {
        process_pending_route_requests(&mgr);

        auto now = std::chrono::steady_clock::now();

        if (!connected_.load()) {
            if (now - last_connect_try >= std::chrono::seconds(3)) {
                last_connect_try = now;
                std::string target_url = config_.server_url;
                if (target_url.find("client_id=") == std::string::npos) {
                    target_url += (target_url.find('?') == std::string::npos ? "?" : "&");
                    target_url += "client_id=" + config_.client_id;
                }

                {
                    std::lock_guard<std::recursive_mutex> lock(mutex_);
                    status_message_ = "Connecting to " + target_url + "...";
                }

                std::cout << "[MeshWS] Attempting WebSocket connection to: " << target_url << std::endl;
                active_conn = mg_ws_connect(&mgr, target_url.c_str(), mg_mesh_event_handler, this, nullptr);
                if (active_conn) {
                    if (mg_url_is_ssl(target_url.c_str())) {
                        struct mg_tls_opts opts{};
                        struct mg_str host_str = mg_url_host(target_url.c_str());
                        opts.name = host_str;
                        opts.ca = mg_str_n(nullptr, 0);
                        mg_tls_init(active_conn, &opts);
                    }
                } else {
                    std::lock_guard<std::recursive_mutex> lock(mutex_);
                    status_message_ = "Failed to initiate connection to " + target_url;
                    std::cout << "[MeshWS] mg_ws_connect returned NULL for " << target_url << std::endl;
                }
            }
        }

        mg_mgr_poll(&mgr, 50);
    }

    connected_.store(false);
    active_ws_conn_ = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        current_mgr_ = nullptr;
    }
    mg_mgr_free(&mgr);
}

bool rouen_mesh_host::is_connected() const noexcept {
    return connected_.load();
}

bool rouen_mesh_host::is_paired() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return config_.is_paired;
}

std::string rouen_mesh_host::get_status_message() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return status_message_;
}

rouen_mesh_host::config rouen_mesh_host::get_config() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return config_;
}

void rouen_mesh_host::set_config(const config& cfg) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    config_ = cfg;
}

bool rouen_mesh_host::open_virtual_route(const std::string& target_client_id, uint16_t target_port, std::string& out_error, uint16_t local_port) {
    if (target_client_id.empty()) {
        out_error = "Target client ID cannot be empty";
        return false;
    }
    if (target_port == 0) {
        out_error = "Target port must be greater than 0";
        return false;
    }

    if (local_port == 0) {
        if (target_port == 8081) {
            local_port = 18081;
        } else if (target_port == 11434) {
            local_port = 21434;
        } else {
            local_port = static_cast<uint16_t>(10000 + target_port);
        }
    }

    if (!is_local_port_available(local_port)) {
        local_port = find_available_mesh_port(local_port);
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);

    uint32_t route_id = next_route_id_++;

    pending_route_requests_.push_back({
        .route_id = route_id,
        .target_client_id = target_client_id,
        .target_port = target_port,
        .local_port = local_port
    });

    std::string local_url = std::format("http://127.0.0.1:{}", local_port);
    mesh::virtual_route_info route_info{
        .route_id = route_id,
        .source_client_id = config_.client_id,
        .target_client_id = target_client_id,
        .target_host = "127.0.0.1",
        .target_port = target_port,
        .local_port = local_port,
        .bytes_transferred = 0,
        .status = "pending",
        .local_url = local_url
    };
    active_routes_[route_id] = route_info;

    if (!running_.load()) {
        start();
    }

    total_requests_++;
    return true;
}

bool rouen_mesh_host::close_virtual_route(uint32_t route_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = active_routes_.find(route_id);
    if (it != active_routes_.end()) {
        active_routes_.erase(it);
        auto listener_it = active_listeners_.find(route_id);
        if (listener_it != active_listeners_.end()) {
            if (listener_it->second && listener_it->second->listener_conn) {
                listener_it->second->listener_conn->is_closing = 1;
            }
            active_listeners_.erase(listener_it);
        }
        return true;
    }
    return false;
}

std::vector<mesh::virtual_route_info> rouen_mesh_host::get_active_routes() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<mesh::virtual_route_info> result;
    result.reserve(active_routes_.size());
    for (const auto& [_, route] : active_routes_) {
        result.push_back(route);
    }
    return result;
}

void rouen_mesh_host::refresh_connected_clients() {
    send_frame_over_ws(mesh::frame_type::CLIENT_LIST_REQ, mesh::frame_flags::NONE, 0, "");
}

std::vector<mesh::mesh_client_dto> rouen_mesh_host::get_connected_clients() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return connected_clients_;
}

void rouen_mesh_host::refresh_server_routes() {
    send_frame_over_ws(mesh::frame_type::ROUTE_LIST_REQ, mesh::frame_flags::NONE, 0, "");
}

uint64_t rouen_mesh_host::get_total_requests() const noexcept {
    return total_requests_.load();
}

uint64_t rouen_mesh_host::get_total_bytes_sent() const noexcept {
    return total_bytes_sent_.load();
}

uint64_t rouen_mesh_host::get_total_bytes_received() const noexcept {
    return total_bytes_received_.load();
}

uint32_t rouen_mesh_host::get_ping_ms() const noexcept {
    return ping_ms_.load();
}

void rouen_mesh_host::register_service(const mesh::mesh_service_info& info) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    local_services_[info.service] = info;
    save_custom_services();

    std::string val_json;
    if (glz::write_json(info, val_json) == glz::error_code::none) {
        mesh::registry_set_dto req{
            .key = std::format("services/{}/{}", config_.client_id, info.service),
            .value = val_json,
            .client_id = config_.client_id,
            .ephemeral = true
        };
        std::string req_json;
        if (glz::write_json(req, req_json) == glz::error_code::none) {
            send_frame_over_ws(mesh::frame_type::REGISTRY_SET, mesh::frame_flags::JSON_PAYLOAD, 0, req_json);
        }
    }
}

void rouen_mesh_host::unregister_service(const std::string& service_name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    local_services_.erase(service_name);
    save_custom_services();

    mesh::registry_set_dto req{
        .key = std::format("services/{}/{}", config_.client_id, service_name),
        .value = "",
        .client_id = config_.client_id,
        .ephemeral = true
    };
    std::string req_json;
    if (glz::write_json(req, req_json) == glz::error_code::none) {
        send_frame_over_ws(mesh::frame_type::REGISTRY_SET, mesh::frame_flags::JSON_PAYLOAD, 0, req_json);
    }
}

void rouen_mesh_host::save_custom_services() {
    std::vector<mesh::mesh_service_info> custom_svcs;
    for (const auto& [name, svc] : local_services_) {
        if (name != "rest_api") {
            custom_svcs.push_back(svc);
        }
    }
    std::string json;
    if (glz::write_json(custom_svcs, json) == glz::error_code::none) {
        auto config_svc = helpers::ConfigService::instance();
        if (config_svc) {
            config_svc->set_env_value("ROUEN_CUSTOM_SERVICES", json, true);
        }
    }
}

void rouen_mesh_host::load_custom_services() {
    auto config_svc = helpers::ConfigService::instance();
    if (!config_svc) return;
    std::string json = config_svc->get_env("ROUEN_CUSTOM_SERVICES");
    if (json.empty()) return;

    std::vector<mesh::mesh_service_info> custom_svcs;
    if (glz::read_json(custom_svcs, json) == glz::error_code::none) {
        for (auto& svc : custom_svcs) {
            svc.client_id = config_.client_id;
            local_services_[svc.service] = svc;
        }
    }
}

void rouen_mesh_host::publish_local_services() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const auto& [_, svc] : local_services_) {
        std::string val_json;
        if (glz::write_json(svc, val_json) == glz::error_code::none) {
            mesh::registry_set_dto req{
                .key = std::format("services/{}/{}", config_.client_id, svc.service),
                .value = val_json,
                .client_id = config_.client_id,
                .ephemeral = true
            };
            std::string req_json;
            if (glz::write_json(req, req_json) == glz::error_code::none) {
                send_frame_over_ws(mesh::frame_type::REGISTRY_SET, mesh::frame_flags::JSON_PAYLOAD, 0, req_json);
            }
        }
    }
}

void rouen_mesh_host::refresh_peer_services() {
    send_frame_over_ws(mesh::frame_type::REGISTRY_LIST, mesh::frame_flags::JSON_PAYLOAD, 0, "");
}

std::vector<mesh::mesh_service_info> rouen_mesh_host::get_local_services() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<mesh::mesh_service_info> result;
    result.reserve(local_services_.size());
    for (const auto& [_, svc] : local_services_) {
        result.push_back(svc);
    }
    return result;
}

std::vector<mesh::mesh_service_info> rouen_mesh_host::get_peer_services() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<mesh::mesh_service_info> result;
    result.reserve(peer_services_.size());
    for (const auto& [_, svc] : peer_services_) {
        result.push_back(svc);
    }
    return result;
}

std::string rouen_mesh_host::generate_handshake_signature(const std::string& client_id, uint64_t timestamp_ms) const {
    std::ostringstream ss;
    ss << client_id << ":" << timestamp_ms;
    std::string payload = ss.str();

    std::stringstream sig_ss;
    for (char c : payload) {
        sig_ss << std::hex << std::setw(2) << std::setfill('0') << (static_cast<int>(c) & 0xFF);
    }
    return sig_ss.str();
}

bool rouen_mesh_host::send_pairing_request(const std::string& server_http_base, const std::string& pairing_code, std::string& out_error) {
    if (pairing_code.empty()) {
        out_error = "Pairing code cannot be empty";
        return false;
    }
    if (server_http_base.empty()) {
        out_error = "Server HTTP base URL cannot be empty";
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (config_.public_key.empty() || config_.private_key.empty()) {
        generate_keypair(config_.public_key, config_.private_key);
    }

    std::string pair_url = server_http_base;
    if (!pair_url.empty() && pair_url.back() == '/') {
        pair_url.pop_back();
    }
    pair_url += "/api/v1/pair";

    std::string json_payload = std::format(
        R"({{"pairing_code":"{}","client_id":"{}","public_key":"{}"}})",
        pairing_code, config_.client_id, config_.public_key
    );

    try {
        http::fetch f(10);
        std::vector<std::string> headers = {"Content-Type: application/json"};
        std::string resp_str = f.post(pair_url, json_payload, headers);

        if (resp_str.find("\"status\": \"paired\"") != std::string::npos || resp_str.find("\"status\":\"paired\"") != std::string::npos) {
            auto config_svc = helpers::ConfigService::instance();
            config_svc->set_env_value("ROUEN_MESH_PAIRED", "1", true);
            config_svc->set_env_value("ROUEN_MESH_CLIENT_ID", config_.client_id, true);
            config_svc->set_env_value("ROUEN_MESH_PUBLIC_KEY", config_.public_key, true);
            config_svc->set_env_value("ROUEN_MESH_PRIVATE_KEY", config_.private_key, true);
            config_svc->set_env_value("ROUEN_MESH_SERVER_URL", config_.server_url, true);

            config_.is_paired = true;
            status_message_ = "Device successfully paired with rouen-service!";
            return true;
        } else {
            out_error = resp_str.empty() ? "Pairing failed: invalid response from server" : resp_str;
            return false;
        }
    } catch (const std::exception& e) {
        out_error = std::string("Pairing network error: ") + e.what();
        return false;
    }
}

void rouen_mesh_host::handle_incoming_frame(const mesh::mesh_frame& frame) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    switch (frame.type) {
        case mesh::frame_type::HEARTBEAT_PING: {
            send_frame_over_ws(mesh::frame_type::HEARTBEAT_PONG, mesh::frame_flags::NONE, 0, frame.payload);
            break;
        }
        case mesh::frame_type::HEARTBEAT_PONG: {
            break;
        }
        case mesh::frame_type::CLIENT_LIST_RESP: {
            if (!frame.payload.empty()) {
                std::vector<mesh::mesh_client_dto> clients;
                if (glz::read_json(clients, frame.payload) == glz::error_code::none) {
                    connected_clients_ = std::move(clients);
                }
            }
            break;
        }
        case mesh::frame_type::REGISTRY_RESP: {
            if (!frame.payload.empty()) {
                std::cout << "[MeshRegistry] Received REGISTRY_RESP: " << frame.payload << std::endl;
                std::vector<mesh::mesh_service_info> direct_services;
                if (glz::read_json(direct_services, frame.payload) == glz::error_code::none && !direct_services.empty() && !direct_services[0].service.empty()) {
                    peer_services_.clear();
                    for (const auto& svc : direct_services) {
                        peer_services_[svc.client_id + ":" + svc.service] = svc;
                    }
                } else {
                    std::vector<mesh::registry_entry_dto> entries;
                    if (glz::read_json(entries, frame.payload) == glz::error_code::none) {
                        peer_services_.clear();
                        for (const auto& entry : entries) {
                            if (!entry.value.empty()) {
                                mesh::mesh_service_info svc;
                                if (glz::read_json(svc, entry.value) == glz::error_code::none && svc.target_port != 0) {
                                    if (svc.client_id.empty()) {
                                        svc.client_id = entry.owner_client_id.empty() ? "server" : entry.owner_client_id;
                                    }
                                    if (svc.service.empty()) {
                                        size_t slash = entry.key.rfind('/');
                                        svc.service = (slash != std::string::npos) ? entry.key.substr(slash + 1) : entry.key;
                                    }
                                    peer_services_[svc.client_id + ":" + svc.service] = svc;
                                } else {
                                    svc.client_id = entry.owner_client_id.empty() ? "server" : entry.owner_client_id;
                                    size_t slash = entry.key.rfind('/');
                                    svc.service = (slash != std::string::npos) ? entry.key.substr(slash + 1) : entry.key;
                                    svc.protocol = "http";

                                    size_t port_pos = entry.value.find("port\":");
                                    if (port_pos == std::string::npos) port_pos = entry.value.find("port :");
                                    if (port_pos != std::string::npos) {
                                        size_t val_start = entry.value.find_first_of("0123456789", port_pos);
                                        if (val_start != std::string::npos) {
                                            svc.target_port = static_cast<uint16_t>(std::atoi(entry.value.c_str() + val_start));
                                        }
                                    }
                                    if (svc.target_port != 0) {
                                        peer_services_[svc.client_id + ":" + svc.service] = svc;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
        case mesh::frame_type::ROUTE_DATA: {
            auto it = active_streams_.find(frame.route_id);
            if (it != active_streams_.end()) {
                auto stream = it->second;
                if (frame.is_flag_set(mesh::frame_flags::INBOUND_DIR)) {
                    if (stream->local_conn) {
                        mg_send(stream->local_conn, frame.payload.data(), frame.payload.size());
                        total_bytes_received_ += frame.payload.size();
                        stream->bytes_received += frame.payload.size();
                    }
                } else {
                    if (stream->target_conn) {
                        mg_send(stream->target_conn, frame.payload.data(), frame.payload.size());
                        total_bytes_sent_ += frame.payload.size();
                        stream->bytes_sent += frame.payload.size();
                    }
                }
            }
            break;
        }
        case mesh::frame_type::ROUTE_OPEN: {
            uint16_t target_port = 8081;
            if (!frame.payload.empty() && frame.payload.find("target_port") != std::string::npos) {
                size_t p = frame.payload.find("target_port\":");
                if (p != std::string::npos) {
                    try {
                        target_port = static_cast<uint16_t>(std::stoi(frame.payload.substr(p + 13)));
                    } catch (...) {}
                }
            }

            std::shared_ptr<mesh::route_stream_ctx> stream;
            auto it = active_streams_.find(frame.route_id);
            if (it != active_streams_.end()) {
                stream = it->second;
                stream->target_port = target_port;
            } else {
                stream = std::make_shared<mesh::route_stream_ctx>();
                stream->route_id = frame.route_id;
                stream->target_port = target_port;
                stream->is_inbound = true;
                active_streams_[frame.route_id] = stream;
            }

            if (current_mgr_ && !stream->target_conn) {
                std::string target_url = std::format("tcp://127.0.0.1:{}", target_port);
                struct mg_connection* target_conn = mg_connect(current_mgr_, target_url.c_str(), mg_inbound_target_handler, stream.get());
                if (target_conn) {
                    stream->target_conn = target_conn;
                    std::cout << "[Mesh] Inbound route #" << frame.route_id << " connected to local target " << target_url << std::endl;
                } else {
                    std::cerr << "[Mesh] Failed to connect inbound route #" << frame.route_id << " to " << target_url << std::endl;
                    std::string fail_ack = R"({"status":"error","reason":"Failed to connect to local target"})";
                    send_frame_over_ws(mesh::frame_type::ROUTE_OPEN_ACK, mesh::frame_flags::JSON_PAYLOAD, frame.route_id, fail_ack);
                }
            }
            break;
        }
        case mesh::frame_type::ROUTE_CLOSE: {
            auto it = active_streams_.find(frame.route_id);
            if (it != active_streams_.end()) {
                auto stream = it->second;
                if (stream->local_conn) {
                    if (stream->local_conn->send.len == 0) {
                        stream->local_conn->is_closing = 1;
                    } else {
                        stream->local_conn->is_draining = 1;
                    }
                }
                if (stream->target_conn) {
                    if (stream->target_conn->send.len == 0) {
                        stream->target_conn->is_closing = 1;
                    } else {
                        stream->target_conn->is_draining = 1;
                    }
                }
            }
            break;
        }
        case mesh::frame_type::HTTP_REQUEST:
        case mesh::frame_type::HTTP_RESPONSE:
        case mesh::frame_type::HTTP_RESPONSE_CHUNK:
        case mesh::frame_type::REGISTRY_SET:
        case mesh::frame_type::REGISTRY_GET:
        case mesh::frame_type::REGISTRY_LIST:
        case mesh::frame_type::PAIRING_REQ:
        case mesh::frame_type::PAIRING_RESP:
        case mesh::frame_type::CLIENT_LIST_REQ:
        case mesh::frame_type::ROUTE_OPEN_ACK:
        case mesh::frame_type::ROUTE_LIST_REQ:
        case mesh::frame_type::ROUTE_LIST_RESP:
        case mesh::frame_type::RAW_DATA:
        default:
            break;
    }
}

void rouen_mesh_host::clear() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    local_services_.clear();
    peer_services_.clear();
    active_routes_.clear();
    active_listeners_.clear();
    active_streams_.clear();
    pending_route_requests_.clear();
    connected_clients_.clear();
    config_ = {};
    active_ws_conn_ = nullptr;
    total_requests_.store(0);
    total_bytes_sent_.store(0);
    total_bytes_received_.store(0);
    status_message_ = "Disconnected";
    initialized_.store(false);
    running_.store(false);
    connected_.store(false);
}

} // namespace rouen::hosts
