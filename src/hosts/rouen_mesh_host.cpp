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
#include "../helpers/debug.hpp"
#include "../helpers/fetch.hpp"
#include "../helpers/notify_service.hpp"
#include "../registrar.hpp"

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

    std::string auth_mode_env = config_svc->get_env("ROUEN_MESH_AUTH_MODE");
    if (config_svc->get_env("ROUEN_MESH_FORCE_UNAUTHENTICATED") == "1") {
        cfg.auth_mode = mesh_auth_mode::unauthenticated;
    } else if (!auth_mode_env.empty()) {
        cfg.auth_mode = auth_mode_from_string(auth_mode_env);
    } else {
        cfg.auth_mode = mesh_auth_mode::challenge_preferred;
    }

    cfg.token = config_svc->get_env("ROUEN_MESH_TOKEN");
    if (cfg.token.empty()) {
        cfg.token = config_svc->get_env("ROUEN_CLIENT_TOKEN");
    }

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

    // Default register Rouen internal REST API (only for full nodes, not transient CLI clients)
    if (config_.client_id.find("-cli-") == std::string::npos) {
        mesh::mesh_service_info api_service{
            .client_id = config_.client_id,
            .service = "rest_api",
            .protocol = "http",
            .target_port = config_.local_api_port,
            .capabilities = {"adaptive_cards", "deck_control", "process_ui"},
            .auth_required = false
        };
        local_services_["rest_api"] = api_service;
    }

    // Auto-register RDP service if explicitly enabled or configured
    auto config_svc = helpers::ConfigService::instance();
    if (config_svc && config_svc->get_env("ROUEN_MESH_EXPOSE_RDP") == "1") {
        mesh::mesh_service_info rdp_service{
            .client_id = config_.client_id,
            .service = "rdp",
            .protocol = "rdp",
            .target_port = 3389,
            .capabilities = {"remote_desktop", "windows_rdp"},
            .auth_required = true
        };
        local_services_["rdp"] = rdp_service;
    }

    // Load persisted user-configured custom mesh services
    load_custom_services();

    // Load persisted user-configured custom virtual routes
    load_custom_routes();

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
        MESH_TRACE("[MeshWS] MG_EV_OPEN: Connection object created.");
    } else if (ev == MG_EV_CONNECT) {
        MESH_TRACE("[MeshWS] MG_EV_CONNECT: TCP Socket connected.");
    } else if (ev == MG_EV_WS_OPEN) {
        MESH_INFO("[MeshWS] MG_EV_WS_OPEN: WebSocket Handshake complete!");
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
        std::string recv_snippet;
        if (c && c->recv.buf && c->recv.len > 0) {
            size_t len = std::min(c->recv.len, static_cast<size_t>(256));
            recv_snippet = std::string(reinterpret_cast<const char*>(c->recv.buf), len);
        }
        MESH_WARN_FMT("[MeshWS] MG_EV_ERROR: {} | recv: '{}'", err_msg ? err_msg : "unknown error", recv_snippet);
        std::string disconnect_reason = err_msg ? err_msg : "WebSocket error";
        if (recv_snippet.find("401") != std::string::npos || recv_snippet.find("Unauthorized") != std::string::npos) {
            if (recv_snippet.find("expired") != std::string::npos || recv_snippet.find("skew") != std::string::npos) {
                disconnect_reason = "Unauthorized (401) - Handshake timestamp expired or clock skew > 60s";
            } else if (recv_snippet.find("replay") != std::string::npos) {
                disconnect_reason = "Unauthorized (401) - Handshake challenge replay detected";
            } else if (recv_snippet.find("not paired") != std::string::npos) {
                disconnect_reason = "Unauthorized (401) - Device is not paired with rouen-service";
            } else if (recv_snippet.find("signature") != std::string::npos) {
                disconnect_reason = "Unauthorized (401) - Invalid Ed25519 challenge signature";
            } else {
                disconnect_reason = "Unauthorized (401) - Handshake authentication failed";
            }
        }
        host->on_ws_disconnected(disconnect_reason);
    } else if (ev == MG_EV_CLOSE) {
        MESH_DEBUG("[MeshWS] MG_EV_CLOSE triggered");
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
    force_reconnect_.store(false);

    std::string auth_desc = (config_.auth_mode == mesh_auth_mode::unauthenticated)
        ? "unauthenticated"
        : (config_.private_key.empty() ? "unauthenticated (no key)" : "challenge-authenticated");
    MESH_INFO_FMT("[MeshWS] WebSocket connected to rouen-service [{}]", auth_desc);
    status_message_ = "Connected to rouen-service (" + auth_desc + ")";

    current_reconnect_backoff_ = std::chrono::milliseconds(1000);
    auto now = std::chrono::steady_clock::now();
    last_frame_received_ = now;
    last_ping_sent_ = now;

    // Self-healing: verify all active mapped route listeners are listening
    verify_and_restore_listeners();

    // Publish own local services and request fresh online clients and peer services
    publish_local_services();
    refresh_connected_clients();
    refresh_peer_services();
    refresh_server_routes();
    refresh_registry();

    // Flush any streams queued while WS was reconnecting
    flush_pending_ws_streams();
}

void rouen_mesh_host::on_ws_disconnected(const std::string& reason) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    connected_.store(false);
    active_ws_conn_ = nullptr;
    status_message_ = "Disconnected: " + reason;

    for (auto& [_, route] : active_routes_) {
        if (route.status == "listening" || route.status == "active" || route.status == "pending") {
            route.status = "listening (reconnecting)";
        }
    }

    // Clean up stale stream sockets (WS multiplex tunnel was severed)
    for (auto& [id, stream] : active_streams_) {
        if (stream->local_conn) {
            stream->local_conn->is_closing = 1;
        }
        if (stream->target_conn) {
            stream->target_conn->is_closing = 1;
        }
    }
    active_streams_.clear();
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
        MESH_DEBUG_FMT("[MeshTunnel] Listener accepted TCP connection on local port {} -> stream route #{}", ctx->local_port, stream_route_id);

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

        bool is_ws_active = false;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            active_streams_[stream_route_id] = stream;
            is_ws_active = connected_.load() && (active_ws_conn_ != nullptr);
            if (!is_ws_active) {
                MESH_INFO_FMT("[MeshSelfHealing] WS disconnected, queueing stream route #{} until connection re-established", stream_route_id);
                pending_streams_awaiting_ws_.push_back(stream);
            }
        }

        if (is_ws_active) {
            std::string open_payload = std::format(
                R"({{"target_client_id":"{}","target_port":{}}})",
                ctx->target_client_id, ctx->target_port
            );
            send_frame_over_ws(mesh::frame_type::ROUTE_OPEN, mesh::frame_flags::JSON_PAYLOAD, stream_route_id, open_payload);
        }
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
            MESH_TRACE_FMT("[MeshTunnel] Outbound client sent {} bytes on route #{}", payload.size(), ctx->route_id);
            
            total_bytes_sent_ += payload.size();
            total_requests_++;
            ctx->bytes_sent += payload.size();

            send_frame_over_ws(mesh::frame_type::ROUTE_DATA, mesh::frame_flags::NONE, ctx->route_id, payload);

            mg_iobuf_del(&c->recv, 0, c->recv.len);
        }
    } else if (ev == MG_EV_CLOSE) {
        MESH_DEBUG_FMT("[MeshTunnel] Outbound client closed connection on route #{}", ctx->route_id);
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
        MESH_DEBUG_FMT("[MeshTunnel] Inbound target TCP connected to target port {} for route #{}", ctx->target_port, ctx->route_id);
        std::string ack = R"({"status":"ok","reason":"Connected to local service target"})";
        send_frame_over_ws(mesh::frame_type::ROUTE_OPEN_ACK, mesh::frame_flags::JSON_PAYLOAD, ctx->route_id, ack);
    } else if (ev == MG_EV_READ) {
        if (c->recv.buf && c->recv.len > 0) {
            std::string_view payload(reinterpret_cast<const char*>(c->recv.buf), c->recv.len);
            MESH_TRACE_FMT("[MeshTunnel] Inbound target service read {} bytes on route #{}", payload.size(), ctx->route_id);

            total_bytes_received_ += payload.size();
            ctx->bytes_received += payload.size();

            send_frame_over_ws(mesh::frame_type::ROUTE_DATA, mesh::frame_flags::INBOUND_DIR, ctx->route_id, payload);

            mg_iobuf_del(&c->recv, 0, c->recv.len);
        }
    } else if (ev == MG_EV_CLOSE) {
        MESH_DEBUG_FMT("[MeshTunnel] Inbound target service closed connection on route #{}", ctx->route_id);
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
            MESH_INFO_FMT("[Mesh] Transparent TCP listener active on {} -> {}:{}", listen_url, req.target_client_id, req.target_port);
        } else {
            auto it = active_routes_.find(route_id);
            if (it != active_routes_.end()) {
                it->second.status = "failed (port in use)";
            }
            MESH_ERROR_FMT("[Mesh] Failed to bind transparent TCP listener on {}", listen_url);
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
    auto last_connect_try = std::chrono::steady_clock::now() - std::chrono::seconds(60);

    while (running_.load()) {
        process_pending_route_requests(&mgr);

        auto now = std::chrono::steady_clock::now();

        check_pending_ws_streams();
        check_heartbeat_and_reconnect(&mgr, now);

        bool forced = force_reconnect_.exchange(false);

        if (!connected_.load() || forced) {
            if (forced || (now - last_connect_try >= current_reconnect_backoff_)) {
                last_connect_try = now;
                current_reconnect_backoff_ = std::min(current_reconnect_backoff_ * 2, std::chrono::milliseconds(15000));

                std::string target_url = build_websocket_url();

                {
                    std::lock_guard<std::recursive_mutex> lock(mutex_);
                    status_message_ = "Connecting to " + target_url + "...";
                }

                MESH_DEBUG_FMT("[MeshWS] Attempting WebSocket connection to: {}", target_url);
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
                    MESH_WARN_FMT("[MeshWS] mg_ws_connect returned NULL for {}", target_url);
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
    save_custom_routes();

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
        save_custom_routes();
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

void rouen_mesh_host::set_registry_value(const std::string& key, const std::string& value, bool ephemeral) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    uint64_t now_sec = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    mesh::registry_entry_dto entry{
        .key = key,
        .value = value,
        .owner_client_id = config_.client_id,
        .created_at_sec = now_sec,
        .updated_at_sec = now_sec,
        .is_ephemeral = ephemeral
    };
    registry_entries_[key] = entry;

    mesh::registry_set_dto req{
        .key = key,
        .value = value,
        .client_id = config_.client_id,
        .ephemeral = ephemeral
    };
    std::string req_json;
    if (glz::write_json(req, req_json) == glz::error_code::none) {
        send_frame_over_ws(mesh::frame_type::REGISTRY_SET, mesh::frame_flags::JSON_PAYLOAD, 0, req_json);
    }
}

void rouen_mesh_host::delete_registry_value(const std::string& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    registry_entries_.erase(key);

    mesh::registry_set_dto req{
        .key = key,
        .value = "",
        .client_id = config_.client_id,
        .ephemeral = true
    };
    std::string req_json;
    if (glz::write_json(req, req_json) == glz::error_code::none) {
        send_frame_over_ws(mesh::frame_type::REGISTRY_SET, mesh::frame_flags::JSON_PAYLOAD, 0, req_json);
    }
}

void rouen_mesh_host::refresh_registry() {
    send_frame_over_ws(mesh::frame_type::REGISTRY_LIST, mesh::frame_flags::JSON_PAYLOAD, 0, "");
}

std::optional<std::string> rouen_mesh_host::get_registry_value(const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = registry_entries_.find(key);
    if (it != registry_entries_.end()) {
        return it->second.value;
    }
    return std::nullopt;
}

std::unordered_map<std::string, mesh::registry_entry_dto> rouen_mesh_host::get_registry_entries(std::string_view prefix) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (prefix.empty()) {
        return registry_entries_;
    }
    std::unordered_map<std::string, mesh::registry_entry_dto> filtered;
    for (const auto& [k, v] : registry_entries_) {
        if (k.starts_with(prefix)) {
            filtered[k] = v;
        }
    }
    return filtered;
}

bool rouen_mesh_host::send_mesh_notification(const std::string& target_client_id, const std::string& message, bool spoken) {
    if (target_client_id.empty() || message.empty()) {
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto now_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    mesh::inbox_notification_dto notif{
        .message = message,
        .from_client = config_.client_id,
        .spoken = spoken,
        .timestamp_ms = now_ms
    };

    std::string payload_json;
    if (glz::write_json(notif, payload_json) != glz::error_code::none) {
        return false;
    }

    static std::atomic<uint64_t> notif_seq{0};
    std::string key = std::format("notifications/inbox/{}/{}_{}", target_client_id, now_ms, notif_seq.fetch_add(1, std::memory_order_relaxed));
    set_registry_value(key, payload_json, true);
    MESH_INFO_FMT("[MeshNotify] Sent notification to target client '{}' at key '{}'", target_client_id, key);
    return true;
}

void rouen_mesh_host::process_incoming_notifications() {
    std::vector<std::pair<std::string, mesh::inbox_notification_dto>> pending;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::string prefix = std::format("notifications/inbox/{}/", config_.client_id);

        for (const auto& [k, entry] : registry_entries_) {
            if (k.starts_with(prefix) && !entry.value.empty()) {
                mesh::inbox_notification_dto notif{};
                if (glz::read_json(notif, entry.value) == glz::error_code::none) {
                    pending.push_back({k, notif});
                }
            }
        }

        // Delete keys immediately so they are not re-processed
        for (const auto& [key, _] : pending) {
            delete_registry_value(key);
        }
    }

    // Deliver notifications outside of lock
    for (const auto& [_, notif] : pending) {
        MESH_INFO_FMT("[MeshNotify] Received incoming notification from '{}': {}", notif.from_client, notif.message);

        auto notify_fn = registrar::try_get<std::function<void(std::string const&)>>("notify");
        if (notify_fn) {
            (*notify_fn)(notif.message);
        } else if (notif.spoken) {
            notify_service::speak_notification(notif.message);
        }
    }
}

void rouen_mesh_host::register_service(const mesh::mesh_service_info& info) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    local_services_[info.service] = info;
    save_custom_services();

    std::string val_json;
    if (glz::write_json(info, val_json) == glz::error_code::none) {
        set_registry_value(std::format("services/{}/{}", config_.client_id, info.service), val_json, true);
    }
}

void rouen_mesh_host::unregister_service(const std::string& service_name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    local_services_.erase(service_name);
    save_custom_services();

    delete_registry_value(std::format("services/{}/{}", config_.client_id, service_name));
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
            set_registry_value(std::format("services/{}/{}", config_.client_id, svc.service), val_json, true);
        }
    }
}

void rouen_mesh_host::refresh_peer_services() {
    refresh_registry();
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

bool rouen_mesh_host::is_auto_connect_enabled() const {
    auto config_svc = helpers::ConfigService::instance();
    if (!config_svc) return true;
    std::string val = config_svc->get_env("ROUEN_MESH_AUTO_CONNECT");
    if (val.empty()) {
        return config_svc->get_env("ROUEN_MESH_PAIRED") == "1";
    }
    return val == "1" || val == "true";
}

void rouen_mesh_host::set_auto_connect_enabled(bool enabled) {
    auto config_svc = helpers::ConfigService::instance();
    if (config_svc) {
        config_svc->set_env_value("ROUEN_MESH_AUTO_CONNECT", enabled ? "1" : "0", true);
    }
}

bool rouen_mesh_host::is_rdp_service_exposed() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return local_services_.find("rdp") != local_services_.end();
}

void rouen_mesh_host::expose_rdp_service(bool enable) {
    auto config_svc = helpers::ConfigService::instance();
    if (enable) {
        mesh::mesh_service_info rdp_service{
            .client_id = config_.client_id,
            .service = "rdp",
            .protocol = "rdp",
            .target_port = 3389,
            .capabilities = {"remote_desktop", "windows_rdp"},
            .auth_required = true
        };
        register_service(rdp_service);
        if (config_svc) {
            config_svc->set_env_value("ROUEN_MESH_EXPOSE_RDP", "1", true);
        }
    } else {
        unregister_service("rdp");
        if (config_svc) {
            config_svc->set_env_value("ROUEN_MESH_EXPOSE_RDP", "0", true);
        }
    }
}

std::string rouen_mesh_host::generate_handshake_signature(const std::string& client_id, uint64_t timestamp_ms, const std::string& private_key_hex) const {
    std::string priv_hex = !private_key_hex.empty() ? private_key_hex : config_.private_key;
    if (priv_hex.empty()) {
        return "";
    }

    // 1. Construct canonical message payload: "{client_id}:{timestamp_ms}"
    std::string message = client_id + ":" + std::to_string(timestamp_ms);

    // 2. Decode 64-character hex private key (32 bytes raw seed)
    std::vector<unsigned char> priv_bytes;
    priv_bytes.reserve(32);
    for (size_t i = 0; i + 1 < priv_hex.size() && priv_bytes.size() < 32; i += 2) {
        std::string byte_str = priv_hex.substr(i, 2);
        try {
            priv_bytes.push_back(static_cast<unsigned char>(std::stoul(byte_str, nullptr, 16)));
        } catch (...) {
            MESH_ERROR("[MeshAuth] Invalid hex character in Ed25519 private key.");
            return "";
        }
    }

    if (priv_bytes.size() != 32) {
        MESH_ERROR("[MeshAuth] Private key length invalid, expected 32 bytes (64 hex characters).");
        return "";
    }

    // 3. Load OpenSSL Ed25519 Private Key
    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519,
        nullptr,
        priv_bytes.data(),
        priv_bytes.size()
    );
    if (!pkey) {
        MESH_ERROR("[MeshAuth] Failed to load Ed25519 private key.");
        return "";
    }

    // 4. Sign canonical message using EVP_DigestSign
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    std::string sig_hex;

    if (md_ctx && EVP_DigestSignInit(md_ctx, nullptr, nullptr, nullptr, pkey) == 1) {
        size_t sig_len = 0;
        // Query signature buffer size (always 64 bytes for Ed25519)
        if (EVP_DigestSign(md_ctx, nullptr, &sig_len, reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1) {
            std::vector<unsigned char> sig_buf(sig_len);
            if (EVP_DigestSign(md_ctx, sig_buf.data(), &sig_len, reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1) {
                std::ostringstream ss;
                for (size_t i = 0; i < sig_len; ++i) {
                    ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(sig_buf[i]);
                }
                sig_hex = ss.str();
            }
        }
    }

    if (md_ctx) EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);

    return sig_hex;
}

bool rouen_mesh_host::verify_handshake_signature(const std::string& client_id, uint64_t timestamp_ms, const std::string& signature_hex, const std::string& public_key_hex) {
    if (public_key_hex.size() != 64 || signature_hex.size() != 128) {
        return false;
    }

    std::vector<unsigned char> pub_bytes;
    pub_bytes.reserve(32);
    for (size_t i = 0; i + 1 < public_key_hex.size() && pub_bytes.size() < 32; i += 2) {
        try {
            pub_bytes.push_back(static_cast<unsigned char>(std::stoul(public_key_hex.substr(i, 2), nullptr, 16)));
        } catch (...) {
            return false;
        }
    }
    if (pub_bytes.size() != 32) return false;

    std::vector<unsigned char> sig_bytes;
    sig_bytes.reserve(64);
    for (size_t i = 0; i + 1 < signature_hex.size() && sig_bytes.size() < 64; i += 2) {
        try {
            sig_bytes.push_back(static_cast<unsigned char>(std::stoul(signature_hex.substr(i, 2), nullptr, 16)));
        } catch (...) {
            return false;
        }
    }
    if (sig_bytes.size() != 64) return false;

    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519,
        nullptr,
        pub_bytes.data(),
        pub_bytes.size()
    );
    if (!pkey) return false;

    std::string message = client_id + ":" + std::to_string(timestamp_ms);
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    bool verified = false;

    if (md_ctx && EVP_DigestVerifyInit(md_ctx, nullptr, nullptr, nullptr, pkey) == 1) {
        if (EVP_DigestVerify(md_ctx, sig_bytes.data(), sig_bytes.size(), reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1) {
            verified = true;
        }
    }

    if (md_ctx) EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);

    return verified;
}

std::string rouen_mesh_host::build_websocket_url() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string target_url = config_.server_url;
    if (target_url.find("client_id=") == std::string::npos) {
        target_url += (target_url.find('?') == std::string::npos ? "?" : "&");
        target_url += "client_id=" + config_.client_id;
    }

    if (!config_.token.empty() && target_url.find("token=") == std::string::npos) {
        target_url += "&token=" + config_.token;
    }

    // Include cryptographic challenge when challenge-informed auth is active and key is available
    if (config_.auth_mode != mesh_auth_mode::unauthenticated) {
        if (!config_.private_key.empty()) {
            auto now_sys = std::chrono::system_clock::now();
            uint64_t ts_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now_sys.time_since_epoch()).count()
            );
            std::string sig = generate_handshake_signature(config_.client_id, ts_ms);
            if (!sig.empty()) {
                target_url += "&timestamp=" + std::to_string(ts_ms);
                target_url += "&signature=" + sig;
            } else if (config_.auth_mode == mesh_auth_mode::challenge_enforced) {
                MESH_ERROR("[MeshAuth] Handshake signature generation failed in challenge_enforced mode.");
            }
        } else if (config_.auth_mode == mesh_auth_mode::challenge_enforced) {
            MESH_ERROR("[MeshAuth] Ed25519 private key missing while challenge_enforced mode is active.");
        }
    }

    return target_url;
}

mesh_auth_mode rouen_mesh_host::get_auth_mode() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return config_.auth_mode;
}

void rouen_mesh_host::set_auth_mode(mesh_auth_mode mode) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    config_.auth_mode = mode;
}

std::string rouen_mesh_host::get_token() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return config_.token;
}

void rouen_mesh_host::set_token(const std::string& token) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    config_.token = token;
}

std::string rouen_mesh_host::auth_mode_to_string(mesh_auth_mode mode) {
    switch (mode) {
        case mesh_auth_mode::unauthenticated: return "unauthenticated";
        case mesh_auth_mode::challenge_enforced: return "challenge_enforced";
        case mesh_auth_mode::challenge_preferred:
        default: return "challenge_preferred";
    }
}

mesh_auth_mode rouen_mesh_host::auth_mode_from_string(std::string_view str) {
    if (str == "unauthenticated" || str == "none" || str == "off" || str == "0" || str == "legacy") {
        return mesh_auth_mode::unauthenticated;
    }
    if (str == "challenge_enforced" || str == "enforced" || str == "strict" || str == "required") {
        return mesh_auth_mode::challenge_enforced;
    }
    return mesh_auth_mode::challenge_preferred;
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
    last_frame_received_ = std::chrono::steady_clock::now();

    switch (frame.type) {
        case mesh::frame_type::HEARTBEAT_PING: {
            send_frame_over_ws(mesh::frame_type::HEARTBEAT_PONG, mesh::frame_flags::NONE, 0, frame.payload);
            break;
        }
        case mesh::frame_type::HEARTBEAT_PONG: {
            if (!frame.payload.empty()) {
                try {
                    uint64_t sent_ts = std::stoull(frame.payload);
                    uint64_t now_ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
                    if (now_ts >= sent_ts) {
                        ping_ms_.store(static_cast<uint32_t>(now_ts - sent_ts));
                    }
                } catch (...) {}
            }
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
                MESH_TRACE_FMT("[MeshRegistry] Received REGISTRY_RESP: {}", frame.payload);
                std::vector<mesh::mesh_service_info> direct_services;
                if (glz::read_json(direct_services, frame.payload) == glz::error_code::none && !direct_services.empty() && !direct_services[0].service.empty()) {
                    peer_services_.clear();
                    for (const auto& svc : direct_services) {
                        peer_services_[svc.client_id + ":" + svc.service] = svc;
                        std::string val_json;
                        (void)glz::write_json(svc, val_json);
                        registry_entries_["services/" + svc.client_id + "/" + svc.service] = mesh::registry_entry_dto{
                            .key = "services/" + svc.client_id + "/" + svc.service,
                            .value = val_json,
                            .owner_client_id = svc.client_id,
                            .is_ephemeral = true
                        };
                    }
                } else {
                    std::vector<mesh::registry_entry_dto> entries;
                    if (glz::read_json(entries, frame.payload) == glz::error_code::none) {
                        peer_services_.clear();
                        for (const auto& entry : entries) {
                            if (entry.value.empty()) {
                                registry_entries_.erase(entry.key);
                                continue;
                            }
                            registry_entries_[entry.key] = entry;

                            if (entry.key.starts_with("services/")) {
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
            process_incoming_notifications();
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
                    MESH_DEBUG_FMT("[Mesh] Inbound route #{} connected to local target {}", frame.route_id, target_url);
                } else {
                    MESH_WARN_FMT("[Mesh] Failed to connect inbound route #{} to {}", frame.route_id, target_url);
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
        case mesh::frame_type::REGISTRY_SET: {
            if (!frame.payload.empty()) {
                mesh::registry_set_dto set_req{};
                if (glz::read_json(set_req, frame.payload) == glz::error_code::none) {
                    if (set_req.value.empty()) {
                        registry_entries_.erase(set_req.key);
                    } else {
                        uint64_t now_sec = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                        registry_entries_[set_req.key] = mesh::registry_entry_dto{
                            .key = set_req.key,
                            .value = set_req.value,
                            .owner_client_id = set_req.client_id,
                            .created_at_sec = now_sec,
                            .updated_at_sec = now_sec,
                            .is_ephemeral = set_req.ephemeral
                        };
                    }
                    process_incoming_notifications();
                }
            }
            break;
        }
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

bool rouen_mesh_host::reconnect() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (active_ws_conn_) {
        active_ws_conn_->is_closing = 1;
    }
    current_reconnect_backoff_ = std::chrono::milliseconds(1000);
    force_reconnect_.store(true);
    if (!running_.load()) {
        return start();
    }
    return true;
}

void rouen_mesh_host::save_custom_routes() {
    if (helpers::presence_service::is_headless()) return;
    std::vector<mesh::persistent_route_dto> routes;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (const auto& [_, route] : active_routes_) {
            routes.push_back({
                .target_client_id = route.target_client_id,
                .target_port = route.target_port,
                .local_port = route.local_port
            });
        }
    }
    std::string json;
    if (glz::write_json(routes, json) == glz::error_code::none) {
        auto config_svc = helpers::ConfigService::instance();
        if (config_svc) {
            config_svc->set_env_value("ROUEN_MESH_ROUTES", json, true);
        }
    }
}

void rouen_mesh_host::load_custom_routes() {
    auto config_svc = helpers::ConfigService::instance();
    if (!config_svc) return;
    std::string json = config_svc->get_env("ROUEN_MESH_ROUTES");
    if (json.empty()) return;

    std::vector<mesh::persistent_route_dto> routes;
    if (glz::read_json(routes, json) == glz::error_code::none) {
        for (const auto& r : routes) {
            std::string err;
            open_virtual_route(r.target_client_id, r.target_port, err, r.local_port);
        }
    }
}

void rouen_mesh_host::verify_and_restore_listeners() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (auto& [route_id, route] : active_routes_) {
        auto it = active_listeners_.find(route_id);
        bool listener_alive = (it != active_listeners_.end() && it->second && it->second->listener_conn != nullptr);
        if (!listener_alive) {
            MESH_INFO_FMT("[MeshSelfHealing] Restoring listener for route #{} (127.0.0.1:{}) -> {}:{}",
                          route_id, route.local_port, route.target_client_id, route.target_port);
            pending_route_requests_.push_back({
                .route_id = route_id,
                .target_client_id = route.target_client_id,
                .target_port = route.target_port,
                .local_port = route.local_port
            });
            route.status = "pending";
        } else {
            route.status = "listening";
        }
    }
}

void rouen_mesh_host::flush_pending_ws_streams() {
    std::vector<std::shared_ptr<mesh::route_stream_ctx>> streams_to_send;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (pending_streams_awaiting_ws_.empty()) return;
        streams_to_send.swap(pending_streams_awaiting_ws_);
    }

    for (const auto& stream : streams_to_send) {
        if (stream && stream->local_conn && !stream->local_conn->is_closing) {
            MESH_INFO_FMT("[MeshSelfHealing] Flushing queued stream route #{} over restored WS connection", stream->route_id);
            std::string open_payload = std::format(
                R"({{"target_client_id":"{}","target_port":{}}})",
                stream->target_client_id, stream->target_port
            );
            send_frame_over_ws(mesh::frame_type::ROUTE_OPEN, mesh::frame_flags::JSON_PAYLOAD, stream->route_id, open_payload);
        }
    }
}

void rouen_mesh_host::check_pending_ws_streams() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (pending_streams_awaiting_ws_.empty()) return;
    std::erase_if(pending_streams_awaiting_ws_, [](const auto& stream) {
        return !stream || !stream->local_conn || stream->local_conn->is_closing;
    });
}

void rouen_mesh_host::check_heartbeat_and_reconnect(struct mg_mgr* mgr, std::chrono::steady_clock::time_point now) {
    (void)mgr;
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (connected_.load() && active_ws_conn_) {
        // Send periodic HEARTBEAT_PING frame
        if (last_ping_sent_ == std::chrono::steady_clock::time_point{} ||
            now - last_ping_sent_ >= config_.ping_interval) {
            
            last_ping_sent_ = now;
            uint64_t ts_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
            std::string ping_payload = std::to_string(ts_ms);
            send_frame_over_ws(mesh::frame_type::HEARTBEAT_PING, mesh::frame_flags::NONE, 0, ping_payload);
        }

        // Check for heartbeat timeout (dead/zombie socket detection)
        auto timeout_limit = config_.ping_interval * 2 + std::chrono::seconds(10);
        if (last_frame_received_ != std::chrono::steady_clock::time_point{} &&
            now - last_frame_received_ > timeout_limit) {
            
            MESH_WARN_FMT("[MeshSelfHealing] Heartbeat timeout after {} seconds of silence! Closing zombie WS connection...",
                          std::chrono::duration_cast<std::chrono::seconds>(now - last_frame_received_).count());
            
            if (active_ws_conn_) {
                active_ws_conn_->is_closing = 1;
            }
            on_ws_disconnected("Heartbeat timeout (zombie socket)");
        }
    }
}

void rouen_mesh_host::clear() {
    stop();

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    local_services_.clear();
    peer_services_.clear();
    registry_entries_.clear();
    active_routes_.clear();
    active_listeners_.clear();
    active_streams_.clear();
    pending_route_requests_.clear();
    pending_streams_awaiting_ws_.clear();
    connected_clients_.clear();
    config_ = {};
    active_ws_conn_ = nullptr;
    last_ping_sent_ = {};
    last_frame_received_ = {};
    current_reconnect_backoff_ = std::chrono::milliseconds(1000);
    force_reconnect_.store(false);
    total_requests_.store(0);
    total_bytes_sent_.store(0);
    total_bytes_received_.store(0);
    status_message_ = "Disconnected";
    initialized_.store(false);
    running_.store(false);
    connected_.store(false);
}

} // namespace rouen::hosts
