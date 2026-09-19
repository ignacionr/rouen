/**
 * Test: Rouen Mesh Host & Protocol Infrastructure
 * Purpose: Verifies binary 12-byte frame encoding/decoding, handshake signature generation,
 *          service registration, registry discovery, client node discovery, and virtual route Management.
 * Category: Feature / Integration Test
 */

#include "../src/hosts/rouen_mesh_host.hpp"
#include "../src/helpers/presence_service.hpp"
#include "../src/helpers/notify_service.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace test_helpers {
    void assert_true(bool condition, const std::string& test_name) {
        if (condition) {
            std::cout << "✅ " << test_name << ": PASSED\n";
        } else {
            std::cout << "❌ " << test_name << ": FAILED\n";
            exit(1);
        }
    }

    void assert_equal(size_t expected, size_t actual, const std::string& test_name) {
        if (expected == actual) {
            std::cout << "✅ " << test_name << ": PASSED\n";
        } else {
            std::cout << "❌ " << test_name << ": FAILED (expected " << expected << ", got " << actual << ")\n";
            exit(1);
        }
    }

    void assert_string_equal(const std::string& expected, const std::string& actual, const std::string& test_name) {
        if (expected == actual) {
            std::cout << "✅ " << test_name << ": PASSED\n";
        } else {
            std::cout << "❌ " << test_name << ": FAILED (expected \"" << expected << "\", got \"" << actual << "\")\n";
            exit(1);
        }
    }
}

void test_binary_frame_codec() {
    std::cout << "\n--- Testing Binary 12-Byte Frame Codec ---\n";
    using namespace rouen::mesh;

    test_helpers::assert_equal(12, sizeof(frame_header), "frame_header is exactly 12 bytes");

    // 1. Test Ping Frame Encoding/Decoding
    std::string payload = "ping_payload";
    auto encoded = frame_codec::encode(frame_type::HEARTBEAT_PING, frame_flags::NONE, 0, payload);

    test_helpers::assert_equal(12 + payload.size(), encoded.size(), "Encoded buffer length matches header + payload size");

    auto decoded = frame_codec::decode(encoded.data(), encoded.size());
    test_helpers::assert_true(decoded.has_value(), "Successfully decoded ping frame");
    test_helpers::assert_true(decoded->type == frame_type::HEARTBEAT_PING, "Decoded frame_type is HEARTBEAT_PING");
    test_helpers::assert_equal(0, decoded->route_id, "Decoded route_id is 0");
    test_helpers::assert_string_equal(payload, decoded->payload, "Decoded payload matches original payload");

    // 2. Test ROUTE_OPEN / ROUTE_DATA Binary Frames
    std::string route_data_payload = "GET /v1/models HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    auto data_encoded = frame_codec::encode(frame_type::ROUTE_DATA, frame_flags::NONE, 1001, route_data_payload);
    auto data_decoded = frame_codec::decode(data_encoded.data(), data_encoded.size());

    test_helpers::assert_true(data_decoded.has_value(), "Successfully decoded ROUTE_DATA frame");
    test_helpers::assert_true(data_decoded->type == frame_type::ROUTE_DATA, "Decoded frame_type is ROUTE_DATA");
    test_helpers::assert_equal(1001, data_decoded->route_id, "Decoded route_id matches 1001");
    test_helpers::assert_string_equal(route_data_payload, data_decoded->payload, "Decoded payload matches original TCP request");
}

void test_virtual_route_management() {
    std::cout << "\n--- Testing Virtual Multiplexed Route Management ---\n";
    auto& host = rouen::hosts::rouen_mesh_host::instance();
    host.clear();

    rouen::hosts::rouen_mesh_host::config cfg{};
    cfg.enabled = true;
    cfg.is_paired = true;
    cfg.server_url = "wss://localhost/ws/connect";
    cfg.client_id = "rouen-local-mac";
    host.initialize(cfg);

    std::string err;
    test_helpers::assert_true(!host.open_virtual_route("", 11434, err), "Rejects empty target client ID");
    test_helpers::assert_true(!host.open_virtual_route("peer-node", 0, err), "Rejects port 0");

    bool ok = host.open_virtual_route("peer-node-1", 11434, err, 21434);
    test_helpers::assert_true(ok, "Successfully queued virtual route to peer-node-1:11434");

    uint32_t route_id = 0;
    {
        auto routes = host.get_active_routes();
        test_helpers::assert_equal(1, routes.size(), "Active routes count is 1");
        test_helpers::assert_string_equal("peer-node-1", routes[0].target_client_id, "Target client ID matches");
        test_helpers::assert_equal(11434, routes[0].target_port, "Target port matches");
        test_helpers::assert_equal(21434, routes[0].local_port, "Local port matches 21434");
        route_id = routes[0].route_id;
    }

    test_helpers::assert_true(host.close_virtual_route(route_id), "Successfully closed virtual route");
    test_helpers::assert_equal(0, host.get_active_routes().size(), "Active routes count is 0 after closing");
}

void test_client_node_discovery_parsing() {
    std::cout << "\n--- Testing Client Node Discovery Payload Parsing ---\n";
    auto& host = rouen::hosts::rouen_mesh_host::instance();
    host.clear();

    rouen::hosts::rouen_mesh_host::config cfg{};
    cfg.client_id = "rouen-macbook-pro";
    host.initialize(cfg);

    // Simulate receiving real JSON payload from rouen-service
    std::string json_clients = R"([
        {"client_id":"rouen-macbook-pro","ip_address":"127.0.0.1","user_agent":"Rouen/1.3","uptime_seconds":3600,"last_ping_ago_seconds":2,"requests_tunneled":10,"bytes_sent":1024,"bytes_received":2048},
        {"client_id":"remote-peer-alpha","ip_address":"10.0.0.5","user_agent":"Rouen/1.3","uptime_seconds":7200,"last_ping_ago_seconds":1,"requests_tunneled":50,"bytes_sent":8192,"bytes_received":16384}
    ])";

    rouen::mesh::mesh_frame resp_frame{
        .type = rouen::mesh::frame_type::CLIENT_LIST_RESP,
        .flags = rouen::mesh::frame_flags::JSON_PAYLOAD,
        .route_id = 0,
        .payload = json_clients
    };

    host.handle_incoming_frame(resp_frame);
    auto clients = host.get_connected_clients();

    test_helpers::assert_equal(2, clients.size(), "Parsed 2 online connected mesh nodes from JSON payload");
    test_helpers::assert_string_equal("rouen-macbook-pro", clients[0].client_id, "First client_id matches");
    test_helpers::assert_string_equal("remote-peer-alpha", clients[1].client_id, "Second client_id matches");
}

void test_handshake_signature_generation() {
    std::cout << "\n--- Testing Handshake Signature Generation ---\n";
    auto& host = rouen::hosts::rouen_mesh_host::instance();
    host.clear();

    std::string client_id = "rouen-test-macbook";
    uint64_t ts = 1726162800000;
    std::string sig = host.generate_handshake_signature(client_id, ts);

    test_helpers::assert_true(!sig.empty(), "Generated signature string is non-empty");
}

void test_service_registration_and_discovery_parsing() {
    std::cout << "\n--- Testing Service Discovery Payload Parsing ---\n";
    auto& host = rouen::hosts::rouen_mesh_host::instance();
    host.clear();

    rouen::hosts::rouen_mesh_host::config cfg{};
    cfg.enabled = true;
    cfg.is_paired = true;
    cfg.client_id = "rouen-desktop-mac";
    host.initialize(cfg);

    // Simulate receiving real service discovery JSON payload
    std::string json_services = R"([
        {"client_id":"remote-peer-alpha","service":"llm","protocol":"openai_compatible","target_port":11434,"capabilities":["completions","chat"],"auth_required":false}
    ])";

    rouen::mesh::mesh_frame discovery_resp{
        .type = rouen::mesh::frame_type::REGISTRY_RESP,
        .flags = rouen::mesh::frame_flags::JSON_PAYLOAD,
        .route_id = 0,
        .payload = json_services
    };
    host.handle_incoming_frame(discovery_resp);

    auto peers = host.get_peer_services();
    test_helpers::assert_equal(1, peers.size(), "Discovered 1 peer service from mesh registry JSON");
    test_helpers::assert_string_equal("remote-peer-alpha", peers[0].client_id, "Peer client_id matches");
    test_helpers::assert_string_equal("llm", peers[0].service, "Peer service matches");
}

void test_pairing_request_validation() {
    std::cout << "\n--- Testing Client Pairing Request Flow ---\n";
    auto& host = rouen::hosts::rouen_mesh_host::instance();
    std::string err;

    test_helpers::assert_true(!host.send_pairing_request("", "123456", err), "Rejects empty server URL");
    test_helpers::assert_true(!host.send_pairing_request("https://rouen.inz.dev", "", err), "Rejects empty pairing code");
}

void test_self_healing_resilience() {
    std::cout << "\n--- Testing Mesh Client Self-Healing & Route Persistence ---\n";
    auto& host = rouen::hosts::rouen_mesh_host::instance();
    host.clear();

    rouen::hosts::rouen_mesh_host::config cfg{};
    cfg.enabled = true;
    cfg.is_paired = true;
    cfg.client_id = "rouen-test-node";
    host.initialize(cfg);

    std::string err;
    bool ok = host.open_virtual_route("peer-ollama-node", 11434, err, 21434);
    test_helpers::assert_true(ok, "Opened virtual route 21434 -> peer-ollama-node:11434");

    // Simulate WebSocket disconnect event
    host.on_ws_disconnected("Simulated Network Dropout");
    test_helpers::assert_true(!host.is_connected(), "Host reports disconnected state after dropout");

    auto routes = host.get_active_routes();
    test_helpers::assert_equal(1, routes.size(), "Mapped port remains active in routes map during dropout");
    test_helpers::assert_string_equal("listening (reconnecting)", routes[0].status, "Route status updated to listening (reconnecting)");

    // Simulate WebSocket reconnect event
    host.on_ws_connected(nullptr);
    test_helpers::assert_true(host.is_connected(), "Host reports connected state after reconnect");

    routes = host.get_active_routes();
    test_helpers::assert_equal(1, routes.size(), "Mapped route remains preserved after reconnection");

    // Simulate receiving Heartbeat Pong
    uint64_t past_ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count()) - 45;
    rouen::mesh::mesh_frame pong_frame{
        .type = rouen::mesh::frame_type::HEARTBEAT_PONG,
        .flags = rouen::mesh::frame_flags::NONE,
        .route_id = 0,
        .payload = std::to_string(past_ts)
    };
    host.handle_incoming_frame(pong_frame);
    test_helpers::assert_true(host.get_ping_ms() >= 40, "Calculated ping latency from HEARTBEAT_PONG payload");

    host.stop();
}

void test_mesh_registry_generic_api() {
    std::cout << "\n--- Testing Mesh Registry Generic Key-Value API ---\n";
    auto& host = rouen::hosts::rouen_mesh_host::instance();
    host.clear();

    rouen::hosts::rouen_mesh_host::config cfg{};
    cfg.client_id = "test-node-1";
    host.initialize(cfg);

    // Test setting registry value
    host.set_registry_value("test/key1", "sample_value_1");
    auto val1 = host.get_registry_value("test/key1");
    test_helpers::assert_true(val1.has_value(), "Retrieved registry value for test/key1");
    test_helpers::assert_string_equal("sample_value_1", *val1, "Registry value matches set content");

    // Test prefix filtering
    host.set_registry_value("test/key2", "sample_value_2");
    host.set_registry_value("other/key3", "sample_value_3");

    auto test_entries = host.get_registry_entries("test/");
    test_helpers::assert_equal(2, test_entries.size(), "Filtered 2 entries with prefix test/");

    auto all_entries = host.get_registry_entries();
    test_helpers::assert_true(all_entries.size() >= 3, "Total registry entries contains at least 3 items");

    // Test deleting registry value
    host.delete_registry_value("test/key1");
    auto deleted_val = host.get_registry_value("test/key1");
    test_helpers::assert_true(!deleted_val.has_value(), "Deleted key returns nullopt");
}

void test_presence_service_and_inference() {
    std::cout << "\n--- Testing Presence Service & Mesh Inference ---\n";
    auto& host = rouen::hosts::rouen_mesh_host::instance();
    host.clear();

    rouen::hosts::rouen_mesh_host::config cfg{};
    cfg.client_id = "rouen-desktop-mac";
    host.initialize(cfg);

    auto& ps = rouen::services::presence_service::instance();
    ps.start();

    // 1. Verify local presence initialization
    ps.record_interaction("ui_input");
    ps.publish_presence("ui_input", true);

    test_helpers::assert_string_equal("rouen-desktop-mac", ps.get_local_client_id(), "Local client ID matches configuration");
    test_helpers::assert_string_equal("rouen-desktop-mac", ps.get_last_active_client_id(), "Last active client inferred as local client");
    test_helpers::assert_true(ps.is_local_client_last_active(), "Local client is recognized as last active");
    test_helpers::assert_true(ps.should_notify_locally(), "Local client should handle notification");
    test_helpers::assert_string_equal("rouen-desktop-mac", ps.get_recommended_notification_target(), "Recommended notification target is local client");

    // 2. Simulate remote peer client "rouen-macbook-air" reporting recent activity on the mesh
    uint64_t remote_time = ps.get_local_presence().last_active_epoch_ms + 60000; // 1 minute in the future
    rouen::services::presence_record remote_rec{
        .client_id = "rouen-macbook-air",
        .user = "inz",
        .hostname = "macbook-air",
        .platform = "mac",
        .last_active_epoch_ms = remote_time,
        .last_active_iso = "2026-09-18T19:30:00Z",
        .interaction_type = "keyboard",
        .status = "active"
    };
    std::string remote_json;
    [[maybe_unused]] auto ec1 = glz::write_json(remote_rec, remote_json);

    rouen::services::last_active_summary remote_summary{
        .client_id = "rouen-macbook-air",
        .user = "inz",
        .last_active_epoch_ms = remote_time,
        .last_active_iso = "2026-09-18T19:30:00Z",
        .interaction_type = "keyboard"
    };
    std::string summary_json;
    [[maybe_unused]] auto ec2 = glz::write_json(remote_summary, summary_json);

    // Inject remote presence into mesh registry
    host.set_registry_value("presence/rouen-macbook-air", remote_json);
    host.set_registry_value("presence/last_active", summary_json);

    // 3. Verify presence inference identifies remote peer as where the user is
    test_helpers::assert_string_equal("rouen-macbook-air", ps.get_last_active_client_id(), "Inferred user was last active on remote peer rouen-macbook-air");
    test_helpers::assert_true(!ps.is_local_client_last_active(), "Local client is NOT last active when remote peer was newer");
    test_helpers::assert_string_equal("rouen-macbook-air", ps.get_recommended_notification_target(), "Recommended notification target points to remote peer");

    // 4. Verify notify_service integration queries presence correctly
    test_helpers::assert_string_equal("rouen-macbook-air", notify_service::last_active_client(), "notify_service reports last active client matching presence");
    test_helpers::assert_string_equal("rouen-macbook-air", notify_service::recommended_notification_target(), "notify_service recommends notification target matching presence");

    // 5. User now interacts on the local machine
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ps.record_interaction("ui_input");
    ps.publish_presence("ui_input", true);

    // Simulate local timestamp advancing past remote
    rouen::services::last_active_summary local_summary{
        .client_id = "rouen-desktop-mac",
        .user = "inz",
        .last_active_epoch_ms = remote_time + 10000,
        .last_active_iso = "2026-09-18T19:30:10Z",
        .interaction_type = "ui_input"
    };
    std::string local_summary_json;
    (void)glz::write_json(local_summary, local_summary_json);
    host.set_registry_value("presence/last_active", local_summary_json);
    host.set_registry_value("presence/rouen-desktop-mac", local_summary_json);

    test_helpers::assert_string_equal("rouen-desktop-mac", ps.get_last_active_client_id(), "Presence correctly switches back to local client on user interaction");
    test_helpers::assert_true(ps.is_local_client_last_active(), "Local client is once again last active");
    test_helpers::assert_string_equal("rouen-desktop-mac", ps.get_recommended_notification_target(), "Notification target switches back to local machine");

    ps.stop();
}

int main() {
    std::cout << "Rouen Mesh Host Unit Tests\n";
    std::cout << std::string(50, '=') << "\n";

    try {
        test_binary_frame_codec();
        test_virtual_route_management();
        test_client_node_discovery_parsing();
        test_handshake_signature_generation();
        test_service_registration_and_discovery_parsing();
        test_mesh_registry_generic_api();
        test_presence_service_and_inference();
        test_pairing_request_validation();
        test_self_healing_resilience();

        std::cout << "\n" << std::string(50, '=') << "\n";
        std::cout << "✅ All Rouen Mesh Host unit tests passed successfully!\n";
        std::cout << std::string(50, '=') << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}
