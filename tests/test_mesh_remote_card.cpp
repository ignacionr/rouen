#include <gtest/gtest.h>
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "../src/cards/interface/mesh_card_proxy.hpp"
#include "../src/hosts/rouen_mesh_host.hpp"
#include "../src/hosts/mcp_host.hpp"

namespace rouen::hosts {
    mcp_host::mcp_host() {}
    void mcp_host::register_function(const std::string&, const function_definition&) {}
    void mcp_host::unregister_card_functions(const std::string&) {}
    std::vector<mcp_host::function_definition> mcp_host::get_available_functions() const { return {}; }
    mcp_host::execution_result mcp_host::execute_function(const std::string&, const std::string&) {
        return execution_result(false, "", "Not implemented");
    }
}

using namespace rouen::cards;

// 1. URI Parsing Test: Verify mesh://ws-01/sysinfo, mesh:ws-01/sysinfo, and [ws-01]sysinfo
TEST(MeshRemoteCardTest, UriParsingTest) {
    auto res1 = parse_mesh_uri("mesh://ws-01/sysinfo");
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(res1->client_id, "ws-01");
    EXPECT_EQ(res1->target_card_uri, "sysinfo");
    EXPECT_FALSE(res1->is_navigator);
    EXPECT_FALSE(res1->is_mesh_console);

    auto res2 = parse_mesh_uri("mesh:ws-01/sysinfo");
    ASSERT_TRUE(res2.has_value());
    EXPECT_EQ(res2->client_id, "ws-01");
    EXPECT_EQ(res2->target_card_uri, "sysinfo");
    EXPECT_FALSE(res2->is_navigator);

    auto res3 = parse_mesh_uri("[ws-01]sysinfo");
    ASSERT_TRUE(res3.has_value());
    EXPECT_EQ(res3->client_id, "ws-01");
    EXPECT_EQ(res3->target_card_uri, "sysinfo");
    EXPECT_FALSE(res3->is_navigator);

    // Canonicalization
    EXPECT_EQ(canonicalize_mesh_uri("mesh://ws-01/sysinfo"), "mesh://ws-01/sysinfo");
    EXPECT_EQ(canonicalize_mesh_uri("mesh:ws-01/sysinfo"), "mesh://ws-01/sysinfo");
    EXPECT_EQ(canonicalize_mesh_uri("[ws-01]sysinfo"), "mesh://ws-01/sysinfo");
}

// 2. Node-Only URI Test: Verify mesh://ws-01 and mesh:ws-01/ resolve to node navigator mode for ws-01
TEST(MeshRemoteCardTest, NodeOnlyUriTest) {
    auto res1 = parse_mesh_uri("mesh://ws-01");
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(res1->client_id, "ws-01");
    EXPECT_TRUE(res1->target_card_uri.empty());
    EXPECT_TRUE(res1->is_navigator);

    auto res2 = parse_mesh_uri("mesh:ws-01/");
    ASSERT_TRUE(res2.has_value());
    EXPECT_EQ(res2->client_id, "ws-01");
    EXPECT_TRUE(res2->target_card_uri.empty());
    EXPECT_TRUE(res2->is_navigator);

    auto res3 = parse_mesh_uri("[ws-01]");
    ASSERT_TRUE(res3.has_value());
    EXPECT_EQ(res3->client_id, "ws-01");
    EXPECT_TRUE(res3->target_card_uri.empty());
    EXPECT_TRUE(res3->is_navigator);

    EXPECT_EQ(canonicalize_mesh_uri("mesh://ws-01"), "mesh://ws-01");
    EXPECT_EQ(canonicalize_mesh_uri("mesh:ws-01/"), "mesh://ws-01");
    EXPECT_EQ(canonicalize_mesh_uri("[ws-01]"), "mesh://ws-01");
}

// 3. Complex Remote Locator Test: Verify mesh://ws-01/git:/repos/rouen
TEST(MeshRemoteCardTest, ComplexRemoteLocatorTest) {
    auto res = parse_mesh_uri("mesh://ws-01/git:/repos/rouen");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->client_id, "ws-01");
    EXPECT_EQ(res->target_card_uri, "git:/repos/rouen");
    EXPECT_FALSE(res->is_navigator);

    EXPECT_EQ(canonicalize_mesh_uri("mesh://ws-01/git:/repos/rouen"), "mesh://ws-01/git:/repos/rouen");
    EXPECT_EQ(canonicalize_mesh_uri("[ws-01]git:/repos/rouen"), "mesh://ws-01/git:/repos/rouen");
}

// 4. Open Deck Cards Enumeration Test: Mock GET /api/cards/adaptive response
TEST(MeshRemoteCardTest, OpenDeckCardsEnumerationTest) {
    // Array format
    std::string json_array = R"([
        {
            "index": 0,
            "title": "System Info",
            "uri": "sysinfo",
            "adaptive_card": { "type": "AdaptiveCard", "body": [] }
        },
        {
            "index": 1,
            "title": "FootPrints Issues",
            "uri": "footprints",
            "adaptive_card": { "type": "AdaptiveCard", "body": [] }
        }
    ])";

    auto cards = parse_remote_cards_response(json_array);
    ASSERT_EQ(cards.size(), 2);
    EXPECT_EQ(cards[0].index, 0);
    EXPECT_EQ(cards[0].title, "System Info");
    EXPECT_EQ(cards[0].uri, "sysinfo");
    EXPECT_FALSE(cards[0].adaptive_card_json.empty());
    EXPECT_EQ(cards[1].index, 1);
    EXPECT_EQ(cards[1].title, "FootPrints Issues");
    EXPECT_EQ(cards[1].uri, "footprints");

    // Object with "cards" field format (api_server_host format)
    std::string json_obj = R"({
        "success": true,
        "cards": [
            {
                "index": 0,
                "title": "VC Project",
                "uri": "vcproject:/c/work",
                "adaptive_card": { "type": "AdaptiveCard" }
            }
        ]
    })";

    auto cards2 = parse_remote_cards_response(json_obj);
    ASSERT_EQ(cards2.size(), 1);
    EXPECT_EQ(cards2[0].index, 0);
    EXPECT_EQ(cards2[0].title, "VC Project");
    EXPECT_EQ(cards2[0].uri, "vcproject:/c/work");
}

// 5. Scheme Discovery Download Test: Mock GET /api/schemas response & peer_schemas_cache
TEST(MeshRemoteCardTest, SchemeDiscoveryDownloadTest) {
    std::string schemas_json = R"(["about", "ai_chat", "alarm", "cmake", "footprints", "git", "sysinfo", "vcproject", "weather"])";
    auto schemes = parse_remote_schemas_response(schemas_json);
    ASSERT_EQ(schemes.size(), 9);
    EXPECT_EQ(schemes[0], "about");
    EXPECT_EQ(schemes[4], "footprints");
    EXPECT_EQ(schemes[7], "vcproject");

    auto& cache = peer_schemas_cache::instance();
    cache.set_schemas("ws-01", schemes);
    EXPECT_TRUE(cache.has_schemas("ws-01"));
    auto cached = cache.get_schemas("ws-01");
    EXPECT_EQ(cached.size(), 9);
    EXPECT_EQ(cached[4], "footprints");

    // Filter schemes
    auto filtered = cache.filter_schemas("ws-01", "project");
    ASSERT_EQ(filtered.size(), 1);
    EXPECT_EQ(filtered[0], "vcproject");
}

// 6. Route Lifecycle Mock Test: Mock virtual route creation
TEST(MeshRemoteCardTest, RouteLifecycleMockTest) {
    auto& mesh = rouen::hosts::rouen_mesh_host::instance();
    // Route management lookup
    std::string err;
    bool opened = mesh.open_virtual_route("ws-01", 8081, err, 18081, false, false);
    EXPECT_TRUE(opened);

    auto routes = mesh.get_active_routes();
    bool found = false;
    for (const auto& r : routes) {
        if (r.target_client_id == "ws-01" && r.target_port == 8081) {
            found = true;
            EXPECT_GT(r.local_port, 0);
            EXPECT_EQ(r.local_url, std::format("http://127.0.0.1:{}", r.local_port));
            break;
        }
    }
    EXPECT_TRUE(found);
}

// 7. Action Dispatch Test: Verify action JSON construction
TEST(MeshRemoteCardTest, ActionDispatchTest) {
    std::string target_uri = "sysinfo";
    std::string action_payload = R"({"action":"refresh","target":"metrics"})";

    std::string req_json = build_action_request_json(target_uri, action_payload);
    EXPECT_NE(req_json.find("\"uri\":\"sysinfo\""), std::string::npos);
    EXPECT_NE(req_json.find("\"action\":"), std::string::npos);
    EXPECT_NE(req_json.find("\"refresh\""), std::string::npos);
}

// 7b. Create Card Request Test: Verify proper JSON escaping for paths with backslashes
TEST(MeshRemoteCardTest, CreateCardRequestJsonTest) {
    std::string path_uri = "dir:C:\\WINDOWS\\system32";
    std::string req_json = build_create_card_request_json(path_uri);

    glz::json_t doc;
    auto err = glz::read_json(doc, req_json);
    EXPECT_EQ(err, glz::error_code::none);
    EXPECT_TRUE(doc.holds<glz::json_t::object_t>());
    auto& obj = doc.get<glz::json_t::object_t>();
    ASSERT_TRUE(obj.find("uri") != obj.end());
    EXPECT_EQ(obj["uri"].get<std::string>(), "dir:C:\\WINDOWS\\system32");
}

// 8. JSON Diff Preservation Test: Verify unchanged JSON skips layout rebuild
TEST(MeshRemoteCardTest, JsonDiffPreservationTest) {
    mesh_card_proxy proxy("ws-01", "sysinfo", false);
    EXPECT_EQ(proxy.get_uri(), "mesh://ws-01/sysinfo");
    EXPECT_TRUE(proxy.matches_uri("mesh://ws-01/sysinfo"));
    EXPECT_TRUE(proxy.matches_uri("mesh:ws-01/sysinfo"));
    EXPECT_TRUE(proxy.matches_uri("[ws-01]sysinfo"));
    EXPECT_FALSE(proxy.matches_uri("mesh://ws-02/sysinfo"));
    EXPECT_FALSE(proxy.matches_uri("mesh://ws-01/weather"));

    std::string card_json_v1 = R"({
        "type": "AdaptiveCard",
        "version": "1.5",
        "body": [
            { "type": "TextBlock", "text": "System Info Active" }
        ]
    })";

    bool updated1 = proxy.update_adaptive_json_if_changed(card_json_v1);
    EXPECT_TRUE(updated1); // First time: should parse/update

    // Feed identical JSON
    bool updated2 = proxy.update_adaptive_json_if_changed(card_json_v1);
    EXPECT_FALSE(updated2); // Identical: should preserve diff and not re-parse!

    // Feed updated JSON
    std::string card_json_v2 = R"({
        "type": "AdaptiveCard",
        "version": "1.5",
        "body": [
            { "type": "TextBlock", "text": "System Info CPU 45%" }
        ]
    })";
    bool updated3 = proxy.update_adaptive_json_if_changed(card_json_v2);
    EXPECT_TRUE(updated3); // Different: should update
}

// 9. Node Navigator Matching & Mirroring
TEST(MeshRemoteCardTest, NodeNavigatorMode) {
    mesh_node_navigator nav("ws-01", false);
    EXPECT_EQ(nav.get_uri(), "mesh://ws-01");
    EXPECT_TRUE(nav.matches_uri("mesh://ws-01"));
    EXPECT_TRUE(nav.matches_uri("mesh:ws-01/"));
    EXPECT_TRUE(nav.matches_uri("[ws-01]"));
    EXPECT_FALSE(nav.matches_uri("mesh://ws-02"));
    EXPECT_FALSE(nav.matches_uri("mesh://ws-01/sysinfo"));
}

// 10. Tall Adaptive Card Parsing and Input Preservation Test
TEST(MeshRemoteCardTest, TallAdaptiveCardParsingAndInputPreservationTest) {
    mesh_card_proxy proxy("ws-01", "tall_card", false);

    // Construct a tall adaptive card JSON containing 50 TextBlocks, FactSets, Inputs, and Actions
    std::string tall_json = R"({
        "type": "AdaptiveCard",
        "version": "1.5",
        "body": [
)";
    for (int i = 0; i < 50; ++i) {
        tall_json += std::format(R"({{ "type": "TextBlock", "text": "Line item row #{}" }},)", i);
    }
    tall_json += R"({
        "type": "FactSet",
        "facts": [
            { "title": "Node Host", "value": "ws-ir-01" },
            { "title": "Memory Total", "value": "64 GB" },
            { "title": "OS", "value": "Windows 11 Enterprise" }
        ]
    },
    {
        "type": "Input.Text",
        "id": "node_command",
        "placeholder": "Enter command..."
    },
    {
        "type": "Input.Toggle",
        "id": "dry_run",
        "title": "Dry run mode",
        "value": "false"
    }
    ],
    "actions": [
        {
            "type": "Action.Submit",
            "title": "Run Remote Command",
            "data": { "action": "execute_node_cmd" }
        },
        {
            "type": "Action.OpenUrl",
            "title": "Open Docs",
            "url": "https://rouen.app/docs"
        }
    ]
    })";

    bool updated = proxy.update_adaptive_json_if_changed(tall_json);
    EXPECT_TRUE(updated);

    const auto& doc = proxy.get_bound_document();
    // 50 TextBlocks + 1 FactSet + 1 Input.Text + 1 Input.Toggle = 53 elements in body
    EXPECT_EQ(doc.body.size(), 53);
    EXPECT_EQ(doc.actions.size(), 2);

    // Simulate user editing input state in UI
    proxy.get_input_state().text_values["node_command"] = "Get-Process";
    proxy.get_input_state().toggle_values["dry_run"] = true;

    // Simulate next refresh cycle with identical JSON: must NOT re-parse or clobber user input state
    bool updated_again = proxy.update_adaptive_json_if_changed(tall_json);
    EXPECT_FALSE(updated_again);

    EXPECT_EQ(proxy.get_input_state().text_values["node_command"], "Get-Process");
    EXPECT_EQ(proxy.get_input_state().toggle_values["dry_run"], true);

    // Verify action JSON building with user input state
    std::string action_payload = R"({"action":"execute_node_cmd","command":"Get-Process","dry_run":true})";
    std::string req_json = build_action_request_json("tall_card", action_payload);
    EXPECT_NE(req_json.find("\"uri\":\"tall_card\""), std::string::npos);
    EXPECT_NE(req_json.find("execute_node_cmd"), std::string::npos);
    EXPECT_NE(req_json.find("Get-Process"), std::string::npos);
}
