/**
 * Test: Rouen Mesh Host & Protocol Infrastructure
 * Purpose: Verifies binary 12-byte frame encoding/decoding, handshake signature generation,
 *          service registration, registry discovery, client node discovery, and virtual route Management.
 * Category: Feature / Integration Test
 */

#include "../src/hosts/rouen_mesh_host.hpp"
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

int main() {
    std::cout << "Rouen Mesh Host Unit Tests\n";
    std::cout << std::string(50, '=') << "\n";

    try {
        test_binary_frame_codec();
        test_virtual_route_management();
        test_client_node_discovery_parsing();
        test_handshake_signature_generation();
        test_service_registration_and_discovery_parsing();
        test_pairing_request_validation();

        std::cout << "\n" << std::string(50, '=') << "\n";
        std::cout << "✅ All Rouen Mesh Host unit tests passed successfully!\n";
        std::cout << std::string(50, '=') << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}
