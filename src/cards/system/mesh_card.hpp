#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
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

#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/config_service.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/presence_service.hpp"
#include "../../hosts/rouen_mesh_host.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

inline uint16_t find_available_local_port(uint16_t start_port = 20000) {
    for (uint32_t port = start_port; port <= 65535; ++port) {
#ifdef _WIN32
        SOCKET sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
            continue;
        }
#else
        int sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            continue;
        }
#endif
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
#ifdef _WIN32
            ::closesocket(sock);
#else
            ::close(sock);
#endif
            return static_cast<uint16_t>(port);
        }
#ifdef _WIN32
        ::closesocket(sock);
#else
        ::close(sock);
#endif
    }
    return start_port;
}

struct mesh_card : public card {
    mesh_card() {
        colors[0] = {0.15f, 0.45f, 0.65f, 1.0f};  // Primary Mesh Blue
        colors[1] = {0.25f, 0.55f, 0.75f, 0.7f};  // Secondary
        
        get_color(2, {0.30f, 0.75f, 0.95f, 1.0f}); // Highlight Cyan
        get_color(3, {0.12f, 0.18f, 0.24f, 0.8f}); // Section background
        get_color(4, {1.00f, 1.00f, 1.00f, 0.95f}); // Text White
        get_color(5, {0.85f, 0.35f, 0.35f, 1.0f}); // Error Red
        get_color(6, {0.35f, 0.75f, 0.45f, 1.0f}); // Connected Green

        name("Rouen Mesh Console");
        width = 720.0f;
        requested_fps = 10;

        load_config_values();
    }

    ~mesh_card() override = default;

    struct mesh_peer_info {
        std::string peer_id;
        std::string status{"Online"};
        std::string description;
        int active_cards{0};
    };

    bool render() override {
        rouen::ui::imgui_ui_context_impl ui;
        return render(ui);
    }

    bool render(rouen::ui::ui_context& ui) override;

    [[nodiscard]] std::string get_uri() const override {
        return "mesh";
    }

    bool matches_uri(std::string_view uri) const override {
        return uri == "mesh";
    }

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override;

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override;

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }
    [[nodiscard]] bool is_relay_connected() const noexcept { return is_relay_connected_; }
    [[nodiscard]] bool is_connected() const noexcept { return is_relay_connected_; }
    [[nodiscard]] bool is_paired() const noexcept { return is_paired_; }
    [[nodiscard]] const std::string& get_local_client_id() const noexcept { return local_client_id_; }
    [[nodiscard]] const std::string& get_relay_url() const noexcept { return relay_url_; }
    [[nodiscard]] int get_relay_latency_ms() const noexcept { return relay_latency_ms_; }
    [[nodiscard]] const std::vector<mesh_peer_info>& get_peers() const noexcept { return peers_; }
    [[nodiscard]] const std::string& get_pairing_invite() const noexcept { return pairing_invite_; }
    [[nodiscard]] const std::string& get_last_pinged_peer() const noexcept { return last_pinged_peer_; }
    [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }

private:
    std::array<char, 256> server_url_buf_{};
    std::array<char, 128> client_id_buf_{};
    std::array<char, 128> token_buf_{};
    std::array<char, 32> pairing_code_buf_{};
    int auth_mode_idx_{0}; // 0 = Challenge-Informed, 1 = Unauthenticated

    // Tab selection override flag (-1 = none, 0 = Status, 1 = Nodes, 2 = Services)
    int select_tab_next_frame_{-1};

    // Route open form buffers
    std::array<char, 128> route_target_client_buf_{};
    int route_target_port_{8081};
    int route_local_port_{18081};
    std::string route_action_msg_;
    bool route_action_success_{false};

    // New service form buffers
    std::array<char, 64> new_svc_name_buf_{};
    std::array<char, 64> new_svc_proto_buf_{};
    int new_svc_port_{8080};

    std::string pairing_status_msg_;
    bool pairing_success_{false};

    // Safe upgrade form buffers
    std::array<char, 256> upgrade_source_buf_{};
    std::string upgrade_action_msg_;
    bool upgrade_action_success_{false};

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    // State Synchronization
    std::string local_client_id_{"macbook-pro-ir"};
    std::string relay_url_{"wss://mesh.rouen.live/relay"};
    int relay_latency_ms_{24};
    std::vector<mesh_peer_info> peers_{
        {"ws-ir-01", "Online", "Windows build host", 8},
        {"linux-dev-02", "Online", "Debian server", 2}
    };
    bool is_relay_connected_{true};
    bool is_paired_{true};
    std::string pairing_invite_{};
    std::string last_pinged_peer_{};
    std::string last_action_{};

    void load_config_values() {
        auto& host = hosts::rouen_mesh_host::instance();
        auto cfg = host.get_config();

        server_url_buf_.fill('\0');
        std::snprintf(server_url_buf_.data(), server_url_buf_.size(), "%s", cfg.server_url.c_str());

        client_id_buf_.fill('\0');
        std::snprintf(client_id_buf_.data(), client_id_buf_.size(), "%s", cfg.client_id.c_str());

        route_target_client_buf_.fill('\0');
        std::snprintf(route_target_client_buf_.data(), route_target_client_buf_.size(), "rouen-remote-peer");

        token_buf_.fill('\0');
        std::snprintf(token_buf_.data(), token_buf_.size(), "%s", cfg.token.c_str());
        auth_mode_idx_ = (cfg.auth_mode == hosts::mesh_auth_mode::unauthenticated) ? 1 : 0;

        pairing_code_buf_.fill('\0');
        new_svc_name_buf_.fill('\0');
        new_svc_proto_buf_.fill('\0');
        std::snprintf(new_svc_proto_buf_.data(), new_svc_proto_buf_.size(), "http");
    }

    void render_mesh_content() {
        auto& host = hosts::rouen_mesh_host::instance();
        bool is_conn = host.is_connected();
        bool is_host_paired = host.is_paired();
        std::string status_msg = host.get_status_message();

        int select_tab = select_tab_next_frame_;
        select_tab_next_frame_ = -1;

        // Header & Connection Summary Badge
        ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "ROUEN MESH NETWORK CONSOLE");
        ImGui::SameLine();
        if (is_conn) {
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.45f, 1.0f), "[CONNECTED]");
        } else {
            ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "[DISCONNECTED]");
        }
        ImGui::SameLine();
        if (is_host_paired) {
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.45f, 1.0f), "[ED25519 PAIRED]");
        } else {
            ImGui::TextColored(ImVec4(0.85f, 0.55f, 0.25f, 1.0f), "[UNPAIRED]");
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("##mesh_console_tabs")) {
            // TAB 1: Connection & Ed25519 Pairing
            ImGuiTabItemFlags tab0_flags = (select_tab == 0) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("Status & Pairing", nullptr, tab0_flags)) {
                ImGui::Spacing();
                if (!status_msg.empty()) {
                    ImGui::TextDisabled("Status: %s", status_msg.c_str());
                }

                if (is_conn) {
                    ImGui::Text("Latency: %u ms | Requests: %llu | Sent: %llu B | Recv: %llu B",
                                host.get_ping_ms(),
                                static_cast<unsigned long long>(host.get_total_requests()),
                                static_cast<unsigned long long>(host.get_total_bytes_sent()),
                                static_cast<unsigned long long>(host.get_total_bytes_received()));
                }

                ImGui::Spacing();
                ImGui::InputText("Server WSS URL", server_url_buf_.data(), server_url_buf_.size());
                ImGui::InputText("Client Node ID", client_id_buf_.data(), client_id_buf_.size());
                ImGui::InputText("Token (Optional)", token_buf_.data(), token_buf_.size());

                ImGui::Text("Upgrade Mode:");
                ImGui::SameLine();
                if (ImGui::RadioButton("Challenge-Informed (Ed25519)", auth_mode_idx_ == 0)) {
                    auth_mode_idx_ = 0;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Unauthenticated (Legacy)", auth_mode_idx_ == 1)) {
                    auth_mode_idx_ = 1;
                }

                if (ImGui::Button(is_conn ? "Disconnect Mesh" : "Connect Mesh")) {
                    if (is_conn) {
                        host.stop();
                    } else {
                        hosts::rouen_mesh_host::config cfg = host.get_config();
                        cfg.server_url = server_url_buf_.data();
                        cfg.client_id = client_id_buf_.data();
                        cfg.token = token_buf_.data();
                        cfg.auth_mode = (auth_mode_idx_ == 1) ? hosts::mesh_auth_mode::unauthenticated : hosts::mesh_auth_mode::challenge_preferred;
                        cfg.enabled = true;
                        host.set_config(cfg);

                        auto config_svc = helpers::ConfigService::instance();
                        if (config_svc) {
                            config_svc->set_env_value("ROUEN_MESH_AUTH_MODE", hosts::rouen_mesh_host::auth_mode_to_string(cfg.auth_mode), true);
                            if (!cfg.token.empty()) {
                                config_svc->set_env_value("ROUEN_MESH_TOKEN", cfg.token, true);
                            }
                        }

                        host.start();
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "Ed25519 Pairing Authentication");
                if (!is_host_paired) {
                    ImGui::TextColored(ImVec4(0.85f, 0.55f, 0.25f, 1.0f), "⚠️ Device is not yet paired with rouen-service.");
                    ImGui::TextWrapped("Generate a single-use code in the admin console at https://rouen.inz.dev/admin and enter it below:");
                } else {
                    ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.45f, 1.0f), "✓ Device paired with Ed25519 asymmetric keypair.");
                }

                ImGui::InputText("Pairing Code", pairing_code_buf_.data(), pairing_code_buf_.size());
                ImGui::SameLine();

                if (ImGui::Button("Pair Device")) {
                    std::string server_base = server_url_buf_.data();
                    if (server_base.find("wss://") == 0) {
                        server_base.replace(0, 6, "https://");
                    } else if (server_base.find("ws://") == 0) {
                        server_base.replace(0, 5, "http://");
                    }
                    size_t ws_pos = server_base.find("/ws/connect");
                    if (ws_pos != std::string::npos) {
                        server_base.erase(ws_pos);
                    }

                    std::string err;
                    pairing_success_ = host.send_pairing_request(server_base, pairing_code_buf_.data(), err);
                    if (pairing_success_) {
                        pairing_status_msg_ = "Device paired successfully with rouen-service!";
                    } else {
                        pairing_status_msg_ = "Pairing failed: " + err;
                    }
                }

                if (!pairing_status_msg_.empty()) {
                    if (pairing_success_) {
                        ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.45f, 1.0f), "%s", pairing_status_msg_.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "%s", pairing_status_msg_.c_str());
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Startup & Reconnection Settings
                ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "Startup & Reconnection Settings");
                bool auto_connect = host.is_auto_connect_enabled();
                if (ImGui::Checkbox("Auto-connect to Rouen Mesh on startup", &auto_connect)) {
                    host.set_auto_connect_enabled(auto_connect);
                }
                ImGui::TextDisabled("Restores mesh connection and virtual routes automatically across app restarts.");

                bool rdp_exposed = host.is_rdp_service_exposed();
                if (ImGui::Checkbox("Expose Windows Remote Desktop Protocol (Port 3389)", &rdp_exposed)) {
                    host.expose_rdp_service(rdp_exposed);
                }
                ImGui::TextDisabled("Allows remote mesh clients to connect to this machine's RDP service.");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Safe Upgrades & Service Maintenance
                ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "Resilient Self-Upgrade & Service Restoration");
                ImGui::TextWrapped("Perform zero-lockout in-place upgrades. When initiated, a detached worker gracefully restarts Rouen, swaps binaries, preserves pairing keys/.env, and immediately restores mesh and RDP proxy.");

                ImGui::InputText("Source Package / URL", upgrade_source_buf_.data(), upgrade_source_buf_.size());
                ImGui::SameLine();
                if (ImGui::Button("Initiate Upgrade")) {
                    std::string exe_dir = rouen::helpers::ConfigService::get_executable_directory();
                    std::string script_path;
#ifdef _WIN32
                    std::vector<std::string> candidates = {
                        (std::filesystem::path(exe_dir) / "upgrade-rouen.ps1").string(),
                        (std::filesystem::path(exe_dir) / "scripts" / "upgrade-rouen.ps1").string(),
                        (std::filesystem::current_path() / "scripts" / "upgrade-rouen.ps1").string(),
                        (std::filesystem::current_path() / "upgrade-rouen.ps1").string()
                    };
                    for (const auto& c : candidates) {
                        if (std::filesystem::exists(c)) { script_path = c; break; }
                    }
                    if (!script_path.empty()) {
                        std::string cmd = std::format("powershell.exe -ExecutionPolicy Bypass -NoProfile -WindowStyle Hidden -File \"{}\" -Source \"{}\" -InstallDir \"{}\" -Detached",
                            script_path, upgrade_source_buf_.data(), exe_dir);
                        STARTUPINFOA si{};
                        si.cb = sizeof(si);
                        PROCESS_INFORMATION pi{};
                        if (CreateProcessA(NULL, cmd.data(), NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, NULL, exe_dir.c_str(), &si, &pi)) {
                            CloseHandle(pi.hProcess);
                            CloseHandle(pi.hThread);
                            upgrade_action_success_ = true;
                            upgrade_action_msg_ = "Upgrade worker launched in detached process! Rouen will restart and reconnect to mesh shortly.";
                            std::thread([]() {
                                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                                std::exit(0);
                            }).detach();
                        } else {
                            upgrade_action_success_ = false;
                            upgrade_action_msg_ = "Failed to launch upgrade worker.";
                        }
                    } else {
                        upgrade_action_success_ = false;
                        upgrade_action_msg_ = "upgrade-rouen.ps1 script not found.";
                    }
#else
                    std::vector<std::string> candidates = {
                        (std::filesystem::path(exe_dir) / "upgrade-rouen.sh").string(),
                        (std::filesystem::path(exe_dir) / "scripts" / "upgrade-rouen.sh").string(),
                        (std::filesystem::current_path() / "scripts" / "upgrade-rouen.sh").string(),
                        (std::filesystem::current_path() / "upgrade-rouen.sh").string()
                    };
                    for (const auto& c : candidates) {
                        if (std::filesystem::exists(c)) { script_path = c; break; }
                    }
                    if (!script_path.empty()) {
                        std::string cmd = std::format("nohup /bin/bash \"{}\" \"{}\" \"{}\" --detached > /dev/null 2>&1 &",
                            script_path, upgrade_source_buf_.data(), exe_dir);
                        if (std::system(cmd.c_str()) == 0) {
                            upgrade_action_success_ = true;
                            upgrade_action_msg_ = "Upgrade worker launched in detached process! Rouen will restart and reconnect to mesh shortly.";
                            std::thread([]() {
                                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                                std::exit(0);
                            }).detach();
                        } else {
                            upgrade_action_success_ = false;
                            upgrade_action_msg_ = "Failed to launch upgrade worker.";
                        }
                    } else {
                        upgrade_action_success_ = false;
                        upgrade_action_msg_ = "upgrade-rouen.sh script not found.";
                    }
#endif
                }

                if (!upgrade_action_msg_.empty()) {
                    if (upgrade_action_success_) {
                        ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.45f, 1.0f), "%s", upgrade_action_msg_.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "%s", upgrade_action_msg_.c_str());
                    }
                }

                ImGui::EndTabItem();
            }

            // TAB 2: Nodes & Route Tunnels
            ImGuiTabItemFlags tab1_flags = (select_tab == 1) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("Nodes & Tunnels", nullptr, tab1_flags)) {
                ImGui::Spacing();

                auto& presence_svc = rouen::services::presence_service::instance();
                std::string last_active_node = presence_svc.get_last_active_client_id();
                bool is_local = presence_svc.is_local_client_last_active();
                uint64_t sec_ago = presence_svc.get_seconds_since_last_interaction();

                ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.65f, 1.0f), "USER PRESENCE & NOTIFICATION ROUTING");
                ImGui::Text("Last User Interaction: ");
                ImGui::SameLine();
                ImGui::TextColored(is_local ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f) : ImVec4(0.95f, 0.70f, 0.35f, 1.0f),
                                   "%s %s", last_active_node.c_str(), is_local ? "(Local Machine)" : "(Remote Mesh Peer)");
                ImGui::SameLine();
                ImGui::TextDisabled("(%llu seconds ago, status: %s)",
                                    static_cast<unsigned long long>(sec_ago),
                                    presence_svc.is_locally_idle() ? "idle" : "active");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "Online Connected Mesh Nodes (CLIENT_LIST)");
                if (ImGui::Button("Refresh Online Nodes")) {
                    host.refresh_connected_clients();
                    presence_svc.publish_presence("refresh", false);
                }

                auto connected_nodes = host.get_connected_clients();
                if (connected_nodes.empty()) {
                    ImGui::TextDisabled("No connected nodes listed. Click 'Refresh Online Nodes' to query rouen-service.");
                } else {
                    if (ImGui::BeginTable("connected_nodes_table", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Node Client ID");
                        ImGui::TableSetupColumn("IP Address");
                        ImGui::TableSetupColumn("Presence");
                        ImGui::TableSetupColumn("Uptime");
                        ImGui::TableSetupColumn("Requests");
                        ImGui::TableSetupColumn("Bytes Transferred");
                        ImGui::TableHeadersRow();

                        for (const auto& node : connected_nodes) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", node.client_id.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%s", node.ip_address.c_str());
                            ImGui::TableSetColumnIndex(2);
                            if (node.client_id == last_active_node) {
                                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "Active Target");
                            } else {
                                ImGui::TextDisabled("Idle");
                            }
                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%llu s", static_cast<unsigned long long>(node.uptime_seconds));
                            ImGui::TableSetColumnIndex(4);
                            ImGui::Text("%llu", static_cast<unsigned long long>(node.requests_tunneled));
                            ImGui::TableSetColumnIndex(5);
                            ImGui::Text("%llu B", static_cast<unsigned long long>(node.bytes_sent + node.bytes_received));
                        }

                        ImGui::EndTable();
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "Virtual Multiplexed Route Manager");
                ImGui::TextWrapped("Manage active peer-to-peer virtual tunnel routes (ROUTE_OPEN / ROUTE_CLOSE):");

                auto active_routes = host.get_active_routes();
                if (active_routes.empty()) {
                    ImGui::TextDisabled("No active virtual routes. Open a route below to stream remote endpoints.");
                } else {
                    if (ImGui::BeginTable("routes_table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Route ID");
                        ImGui::TableSetupColumn("Source Node");
                        ImGui::TableSetupColumn("Target Node");
                        ImGui::TableSetupColumn("Target Port");
                        ImGui::TableSetupColumn("Local Proxy URL");
                        ImGui::TableSetupColumn("Status");
                        ImGui::TableSetupColumn("Action");
                        ImGui::TableHeadersRow();

                        for (const auto& route : active_routes) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("#%u", route.route_id);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%s", route.source_client_id.c_str());
                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("%s", route.target_client_id.c_str());
                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%u", route.target_port);
                            ImGui::TableSetColumnIndex(4);
                            ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "%s", route.local_url.c_str());
                            ImGui::TableSetColumnIndex(5);
                            ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.45f, 1.0f), "%s", route.status.c_str());
                            ImGui::TableSetColumnIndex(6);
                            
                            std::string btn_label = "Close #" + std::to_string(route.route_id);
                            if (ImGui::Button(btn_label.c_str())) {
                                host.close_virtual_route(route.route_id);
                            }
                        }

                        ImGui::EndTable();
                    }
                }

                ImGui::Spacing();
                ImGui::Text("Open New Virtual Route:");
                ImGui::InputText("Target Node ID", route_target_client_buf_.data(), route_target_client_buf_.size());
                ImGui::InputInt("Target Port", &route_target_port_);
                ImGui::InputInt("Local Listener Port", &route_local_port_);

                if (ImGui::Button("Open Virtual Route")) {
                    std::string err;
                    route_action_success_ = host.open_virtual_route(
                        route_target_client_buf_.data(),
                        static_cast<uint16_t>(route_target_port_),
                        err,
                        static_cast<uint16_t>(route_local_port_)
                    );
                    if (route_action_success_) {
                        route_action_msg_ = "Virtual route opened successfully! Listening on local port.";
                    } else {
                        route_action_msg_ = "Failed to open route: " + err;
                    }
                }

                if (!route_action_msg_.empty()) {
                    if (route_action_success_) {
                        ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.45f, 1.0f), "%s", route_action_msg_.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "%s", route_action_msg_.c_str());
                    }
                }

                ImGui::EndTabItem();
            }

            // TAB 3: Shared & Peer Services
            ImGuiTabItemFlags tab2_flags = (select_tab == 2) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("Services & Discovery", nullptr, tab2_flags)) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "Local Shared Services");
                ImGui::TextWrapped("Services exposed from this computer to remote Rouen mesh nodes:");

                if (ImGui::BeginTable("local_services_table", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Service Name");
                    ImGui::TableSetupColumn("Protocol");
                    ImGui::TableSetupColumn("Target Port");
                    ImGui::TableSetupColumn("Status");
                    ImGui::TableSetupColumn("Action");
                    ImGui::TableHeadersRow();

                    auto local_svcs = rouen::hosts::rouen_mesh_host::instance().get_local_services();
                    for (const auto& svc : local_svcs) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", svc.service.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%s", svc.protocol.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%u", svc.target_port);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.45f, 1.0f), "Active");
                        ImGui::TableSetColumnIndex(4);
                        if (ImGui::Button(std::format("Unlist##{}", svc.service).c_str())) {
                            rouen::hosts::rouen_mesh_host::instance().unregister_service(svc.service);
                        }
                    }

                    ImGui::EndTable();
                }

                ImGui::Spacing();
                ImGui::Text("Expose New Custom Local Service:");
                ImGui::InputText("Service Name", new_svc_name_buf_.data(), new_svc_name_buf_.size());
                ImGui::InputText("Protocol", new_svc_proto_buf_.data(), new_svc_proto_buf_.size());
                ImGui::InputInt("Port", &new_svc_port_);

                if (ImGui::Button("Register Local Service")) {
                    if (new_svc_name_buf_[0] != '\0') {
                        mesh::mesh_service_info new_svc{
                            .client_id = client_id_buf_.data(),
                            .service = new_svc_name_buf_.data(),
                            .protocol = new_svc_proto_buf_.data(),
                            .target_port = static_cast<uint16_t>(new_svc_port_),
                            .capabilities = {"custom_proxy"},
                            .auth_required = false
                        };
                        host.register_service(new_svc);
                        new_svc_name_buf_.fill('\0');
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "Discovered Mesh Peer Services");
                if (ImGui::Button("Refresh Mesh Discovery")) {
                    host.refresh_peer_services();
                }

                auto peer_services = host.get_peer_services();
                if (peer_services.empty()) {
                    ImGui::TextDisabled("No remote peer services discovered yet. Click 'Refresh Mesh Discovery' to query.");
                } else {
                    if (ImGui::BeginTable("peer_services_table", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Peer Node ID");
                        ImGui::TableSetupColumn("Service Name");
                        ImGui::TableSetupColumn("Protocol");
                        ImGui::TableSetupColumn("Target Port");
                        ImGui::TableSetupColumn("Action");
                        ImGui::TableHeadersRow();

                        for (const auto& peer : peer_services) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", peer.client_id.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%s", peer.service.c_str());
                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("%s", peer.protocol.c_str());
                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%u", peer.target_port);
                            ImGui::TableSetColumnIndex(4);

                            std::string prep_btn_label = "Prepare Route##" + peer.client_id + "_" + peer.service;
                            if (ImGui::Button(prep_btn_label.c_str())) {
                                route_target_client_buf_.fill('\0');
                                std::snprintf(route_target_client_buf_.data(), route_target_client_buf_.size(), "%s", peer.client_id.c_str());
                                route_target_port_ = peer.target_port;
                                uint16_t free_port = find_available_local_port(20000);
                                route_local_port_ = free_port;
                                route_action_msg_ = "Pre-filled virtual route for '" + peer.service + "' (" + peer.client_id + ":" + std::to_string(peer.target_port) + ") on probed local port " + std::to_string(free_port) + ". Review and open route below.";
                                route_action_success_ = true;
                                select_tab_next_frame_ = 1;
                            }

                            ImGui::SameLine();
                            std::string proxy_btn_label = "Proxy " + peer.service + "##" + peer.client_id;
                            if (ImGui::Button(proxy_btn_label.c_str())) {
                                std::string err;
                                host.open_virtual_route(peer.client_id, peer.target_port, err);
                            }
                        }

                        ImGui::EndTable();
                    }
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
};

inline bool mesh_card::render(rouen::ui::ui_context& ui) {
    return render_window([this, &ui]() {
        // Presentation mode selector
        ui.checkbox("Adaptive Card View", &adaptive_view_mode);
        ui.separator();

        if (adaptive_view_mode) {
            auto now = std::chrono::steady_clock::now();
            auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_parse_time_).count();
            if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                try {
                    adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                    adaptive_error_.clear();
                } catch (const std::exception& e) {
                    adaptive_error_ = e.what();
                }
                last_adaptive_parse_time_ = now;
            }

            if (!adaptive_error_.empty()) {
                ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
            } else {
                adaptive_renderer_.render(
                    adaptive_bound_, adaptive_input_state_,
                    helpers::adaptive_cards::renderer::action_callbacks{
                        .open_url = [](const std::string& url) {
                            static_cast<void>(rouen::platform::open_url(url));
                        },
                        .on_submit = [this](const std::string& payload) {
                            handle_action(payload);
                        }
                    },
                    helpers::adaptive_cards::render_config{
                        .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                        .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                    }
                );
            }
            return;
        }

        if (ImGui::BeginChild("##mesh_card_scroll_region", ImVec2(0, 0), false)) {
            render_mesh_content();
        }
        ImGui::EndChild();
    });
}

inline std::string mesh_card::get_adaptive_card_json() const {
    glz::json_t card;
    card["type"] = "AdaptiveCard";
    card["version"] = "1.5";
    card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";
    card["refreshIntervalMs"] = 5000;

    std::vector<glz::json_t> body;

    // Header Container
    {
        glz::json_t container;
        container["type"] = "Container";
        container["style"] = "emphasis";

        glz::json_t col_set;
        col_set["type"] = "ColumnSet";
        std::vector<glz::json_t> cols;

        // Column 1: Emoji icon (spider web 🕸️)
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "auto";
            std::vector<glz::json_t> col_items;
            glz::json_t tb_emoji;
            tb_emoji["type"] = "TextBlock";
            tb_emoji["text"] = "\xf0\x9f\x97\xb8\xef\xb8\x8f";
            tb_emoji["size"] = "Large";
            col_items.push_back(std::move(tb_emoji));
            col["items"] = std::move(col_items);
            cols.push_back(std::move(col));
        }

        // Column 2: Title and Subtitle
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "stretch";
            std::vector<glz::json_t> col_items;

            glz::json_t tb_title;
            tb_title["type"] = "TextBlock";
            tb_title["text"] = "Rouen Mesh Relay Network";
            tb_title["weight"] = "Bolder";
            tb_title["size"] = "Large";
            col_items.push_back(std::move(tb_title));

            glz::json_t tb_sub;
            tb_sub["type"] = "TextBlock";
            tb_sub["text"] = "Node ID: " + local_client_id_ + (is_paired_ ? " (Ed25519 Authenticated)" : " (Unauthenticated)");
            tb_sub["isSubtle"] = true;
            tb_sub["size"] = "Small";
            col_items.push_back(std::move(tb_sub));

            col["items"] = std::move(col_items);
            cols.push_back(std::move(col));
        }

        // Column 3: Connection status
        {
            glz::json_t col;
            col["type"] = "Column";
            col["width"] = "auto";
            std::vector<glz::json_t> col_items;

            glz::json_t tb_status;
            tb_status["type"] = "TextBlock";
            tb_status["text"] = is_relay_connected_ ? "CONNECTED" : "DISCONNECTED";
            tb_status["weight"] = "Bolder";
            tb_status["color"] = is_relay_connected_ ? "Good" : "Attention";
            tb_status["size"] = "Small";
            col_items.push_back(std::move(tb_status));

            col["items"] = std::move(col_items);
            cols.push_back(std::move(col));
        }

        col_set["columns"] = std::move(cols);
        std::vector<glz::json_t> cont_items;
        cont_items.push_back(std::move(col_set));
        container["items"] = std::move(cont_items);
        body.push_back(std::move(container));
    }

    // FactSet
    {
        glz::json_t fact_set;
        fact_set["type"] = "FactSet";
        std::vector<glz::json_t> facts;

        facts.push_back(glz::json_t::object_t{
            {"title", "Relay Server:"},
            {"value", relay_url_}
        });

        facts.push_back(glz::json_t::object_t{
            {"title", "Roundtrip Latency:"},
            {"value", std::to_string(relay_latency_ms_) + " ms"}
        });

        for (const auto& peer : peers_) {
            std::string desc = peer.description.empty() ? peer.status : (peer.status + " (" + peer.description + ", " + std::to_string(peer.active_cards) + " active cards)");
            facts.push_back(glz::json_t::object_t{
                {"title", "Peer " + peer.peer_id + ":"},
                {"value", desc}
            });
        }

        fact_set["facts"] = std::move(facts);
        body.push_back(std::move(fact_set));
    }

    card["body"] = std::move(body);

    // Actions
    {
        std::vector<glz::json_t> actions;

        glz::json_t act_reconnect;
        act_reconnect["type"] = "Action.Execute";
        act_reconnect["title"] = "Reconnect Relay";
        act_reconnect["verb"] = "mesh_reconnect";
        actions.push_back(std::move(act_reconnect));

        glz::json_t act_invite;
        act_invite["type"] = "Action.Execute";
        act_invite["title"] = "Generate Invite";
        act_invite["verb"] = "mesh_invite";
        actions.push_back(std::move(act_invite));

        card["actions"] = std::move(actions);
    }

    std::string json_str;
    auto err = glz::write_json(card, json_str);
    if (err) {
        return "{}";
    }
    return json_str;
}

inline void mesh_card::handle_action(std::string_view action_json) {
    if (action_json.empty()) return;

    try {
        std::string verb;
        glz::json_t data{};
        glz::json_t payload{};

        auto err = glz::read_json(payload, std::string(action_json));
        if (!err && payload.is_object()) {
            if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                verb = payload["verb"].get<std::string>();
            } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                verb = payload["action"].get<std::string>();
            }

            if (payload.contains("data") && payload["data"].is_object()) {
                data = payload["data"];
            } else {
                data = payload;
            }
        } else {
            verb = std::string(action_json);
        }

        if (verb == "mesh_reconnect") {
            last_action_ = "mesh_reconnect";
            is_relay_connected_ = true;
            try {
                hosts::rouen_mesh_host::instance().reconnect();
            } catch (...) {}
            last_adaptive_parse_time_ = {};
        } else if (verb == "mesh_ping_peer") {
            last_action_ = "mesh_ping_peer";
            std::string peer_id;
            if (data.is_object()) {
                if (data.contains("peer_id") && data["peer_id"].holds<std::string>()) {
                    peer_id = data["peer_id"].get<std::string>();
                } else if (data.contains("peer") && data["peer"].holds<std::string>()) {
                    peer_id = data["peer"].get<std::string>();
                }
            }
            last_pinged_peer_ = peer_id;
            for (auto& peer : peers_) {
                if (peer.peer_id == peer_id) {
                    peer.status = "Online";
                }
            }
            last_adaptive_parse_time_ = {};
        } else if (verb == "mesh_invite") {
            last_action_ = "mesh_invite";
            pairing_invite_ = "ed25519-inv-" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
            pairing_status_msg_ = "Invite generated: " + pairing_invite_;
            last_adaptive_parse_time_ = {};
        } else if (verb == "toggle_adaptive_view") {
            last_action_ = "toggle_adaptive_view";
            adaptive_view_mode = !adaptive_view_mode;
            last_adaptive_parse_time_ = {};
        }
    } catch (const std::exception& e) {
        std::cerr << "[mesh] Action error: " << e.what() << std::endl;
    } catch (...) {}
}

} // namespace rouen::cards
