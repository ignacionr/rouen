#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "../../helpers/config_service.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../hosts/rouen_mesh_host.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

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

    bool render() override {
        return render_window([this]() {
            render_mesh_content();
        });
    }

    [[nodiscard]] std::string get_uri() const override {
        return "mesh";
    }

private:
    std::array<char, 256> server_url_buf_{};
    std::array<char, 128> client_id_buf_{};
    std::array<char, 32> pairing_code_buf_{};

    // Route open form buffers
    std::array<char, 128> route_target_client_buf_{};
    int route_target_port_{11434};
    std::string route_action_msg_;
    bool route_action_success_{false};

    // New service form buffers
    std::array<char, 64> new_svc_name_buf_{};
    std::array<char, 64> new_svc_proto_buf_{};
    int new_svc_port_{8080};

    std::string pairing_status_msg_;
    bool pairing_success_{false};

    void load_config_values() {
        auto& host = hosts::rouen_mesh_host::instance();
        auto cfg = host.get_config();

        server_url_buf_.fill('\0');
        std::snprintf(server_url_buf_.data(), server_url_buf_.size(), "%s", cfg.server_url.c_str());

        client_id_buf_.fill('\0');
        std::snprintf(client_id_buf_.data(), client_id_buf_.size(), "%s", cfg.client_id.c_str());

        route_target_client_buf_.fill('\0');
        std::snprintf(route_target_client_buf_.data(), route_target_client_buf_.size(), "rouen-remote-peer");

        pairing_code_buf_.fill('\0');
        new_svc_name_buf_.fill('\0');
        new_svc_proto_buf_.fill('\0');
        std::snprintf(new_svc_proto_buf_.data(), new_svc_proto_buf_.size(), "http");
    }

    void render_mesh_content() {
        auto& host = hosts::rouen_mesh_host::instance();
        bool is_conn = host.is_connected();
        bool is_paired = host.is_paired();
        std::string status_msg = host.get_status_message();

        // 1. Header & Connection Status
        ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "ROUEN MESH NETWORK CONSOLE");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Mesh Connection:");
        ImGui::SameLine();
        if (is_conn) {
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.45f, 1.0f), "[CONNECTED]");
        } else {
            ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "[DISCONNECTED]");
        }

        ImGui::SameLine();
        ImGui::Text("| Auth:");
        ImGui::SameLine();
        if (is_paired) {
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.45f, 1.0f), "[ED25519 PAIRED]");
        } else {
            ImGui::TextColored(ImVec4(0.85f, 0.55f, 0.25f, 1.0f), "[UNPAIRED]");
        }

        if (!status_msg.empty()) {
            ImGui::TextDisabled("Status: %s", status_msg.c_str());
        }

        // Real-time Traffic & Latency Metrics
        if (is_conn) {
            ImGui::Text("Latency: %u ms | Requests: %llu | Sent: %llu B | Recv: %llu B",
                        host.get_ping_ms(),
                        static_cast<unsigned long long>(host.get_total_requests()),
                        static_cast<unsigned long long>(host.get_total_bytes_sent()),
                        static_cast<unsigned long long>(host.get_total_bytes_received()));
        }

        ImGui::InputText("Server WSS URL", server_url_buf_.data(), server_url_buf_.size());
        ImGui::InputText("Client Node ID", client_id_buf_.data(), client_id_buf_.size());

        if (ImGui::Button(is_conn ? "Disconnect Mesh" : "Connect Mesh")) {
            if (is_conn) {
                host.stop();
            } else {
                hosts::rouen_mesh_host::config cfg = host.get_config();
                cfg.server_url = server_url_buf_.data();
                cfg.client_id = client_id_buf_.data();
                cfg.enabled = true;
                host.set_config(cfg);
                host.start();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 2. Ed25519 Admin Pairing Console
        ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "Ed25519 Pairing Authentication");
        if (!is_paired) {
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

        // 3. Online Connected Mesh Nodes Discovery (CLIENT_LIST_RESP)
        ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "Online Connected Mesh Nodes (CLIENT_LIST)");
        if (ImGui::Button("Refresh Online Nodes")) {
            host.refresh_connected_clients();
        }

        auto connected_nodes = host.get_connected_clients();
        if (connected_nodes.empty()) {
            ImGui::TextDisabled("No connected nodes listed. Click 'Refresh Online Nodes' to query rouen-service.");
        } else {
            if (ImGui::BeginTable("connected_nodes_table", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Node Client ID");
                ImGui::TableSetupColumn("IP Address");
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
                    ImGui::Text("%llu s", static_cast<unsigned long long>(node.uptime_seconds));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%llu", static_cast<unsigned long long>(node.requests_tunneled));
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%llu B", static_cast<unsigned long long>(node.bytes_sent + node.bytes_received));
                }

                ImGui::EndTable();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 4. Virtual Route Management Section
        ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "Virtual Multiplexed Route Manager");
        ImGui::TextWrapped("Manage active peer-to-peer virtual tunnel routes (ROUTE_OPEN / ROUTE_CLOSE):");

        auto active_routes = host.get_active_routes();
        if (active_routes.empty()) {
            ImGui::TextDisabled("No active virtual routes. Open a route below to stream remote endpoints.");
        } else {
            if (ImGui::BeginTable("routes_table", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Route ID");
                ImGui::TableSetupColumn("Source Node");
                ImGui::TableSetupColumn("Target Node");
                ImGui::TableSetupColumn("Target Port");
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
                    ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.45f, 1.0f), "%s", route.status.c_str());
                    ImGui::TableSetColumnIndex(5);
                    
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

        if (ImGui::Button("Open Virtual Route")) {
            std::string err;
            route_action_success_ = host.open_virtual_route(
                route_target_client_buf_.data(),
                static_cast<uint16_t>(route_target_port_),
                err
            );
            if (route_action_success_) {
                route_action_msg_ = "Virtual route opened successfully!";
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

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 5. Local Shared Services
        ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.95f, 1.0f), "Local Shared Services");
        ImGui::TextWrapped("Services exposed from this computer to remote Rouen mesh nodes:");

        if (ImGui::BeginTable("local_services_table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Service Name");
            ImGui::TableSetupColumn("Protocol");
            ImGui::TableSetupColumn("Target Port");
            ImGui::TableSetupColumn("Status");
            ImGui::TableHeadersRow();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("rest_api");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("http");
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("8081");
            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.45f, 1.0f), "Active");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("llm");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("openai_compatible");
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("11434");
            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.45f, 1.0f), "Active");

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

        // 6. Discovered Remote Peer Mesh Services
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

                    std::string proxy_btn_label = "Proxy " + peer.service + "##" + peer.client_id;
                    if (ImGui::Button(proxy_btn_label.c_str())) {
                        std::string err;
                        host.open_virtual_route(peer.client_id, peer.target_port, err);
                    }
                }

                ImGui::EndTable();
            }
        }
    }
};

} // namespace rouen::cards
