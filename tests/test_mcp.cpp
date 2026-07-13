#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <format>
#include <algorithm>
#include "../src/helpers/mcp_service.hpp"
#include "../src/helpers/process_helper.hpp"

using namespace rouen::helpers;

TEST(MCPTest, GathersDefaultCommands) {
    mcp_service mcp;
    
    // Check that run_local_command is registered globally by default
    EXPECT_TRUE(mcp.has_function("run_local_command"));
    
    auto functions = mcp.get_available_functions();
    
    // Verify run_local_command schema and definition
    auto it = std::find_if(functions.begin(), functions.end(), [](const auto& f) {
        return f.name == "run_local_command";
    });
    
    ASSERT_NE(it, functions.end());
    EXPECT_EQ(it->card_type, "terminal");
    EXPECT_FALSE(it->description.empty());
    
    // Simulate how AI Chat gathers and formats the schema
    std::string schema = std::format(
        "{{\"name\":\"{}\",\"description\":\"{}\",\"parameters\":{}}}",
        it->name,
        it->description,
        it->schema.empty() ? "{\"type\":\"object\",\"properties\":{}}" : it->schema
    );
    
    // Verify it generates a valid JSON schema string containing expected properties
    EXPECT_TRUE(schema.find("run_local_command") != std::string::npos);
    EXPECT_TRUE(schema.find("command") != std::string::npos);
    EXPECT_TRUE(schema.find("working_directory") != std::string::npos);
}

TEST(MCPTest, DynamicCardRegistration) {
    mcp_service mcp;
    
    // Simulate a card registering a dynamic MCP function
    mcp_service::function_definition dummy_def(
        "git_test_func",
        "Test git dynamic function",
        R"({"type":"object","properties":{}})",
        [](const std::string&) -> std::string {
            return "git_success";
        },
        "git"
    );
    
    mcp.register_function("git", dummy_def);
    
    // Check that both functions are gathered
    EXPECT_TRUE(mcp.has_function("run_local_command"));
    EXPECT_TRUE(mcp.has_function("git_test_func"));
    
    auto functions = mcp.get_available_functions();
    EXPECT_GE(functions.size(), 2u);
    
    // Test execution of dynamic function
    auto result = mcp.execute_function("git_test_func", "{}");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.result, "git_success");
    
    // Test unregistration
    mcp.unregister_card_functions("git");
    EXPECT_TRUE(mcp.has_function("run_local_command"));
    EXPECT_FALSE(mcp.has_function("git_test_func"));
}

TEST(MCPTest, ExecuteLocalCommand) {
    mcp_service mcp;
    
    // Test execution of the default run_local_command function
    // We execute an 'echo' command which is platform independent enough for Unix/macOS
    std::string params = R"({"command":"echo 'mcp_test_output'"})";
    
    auto result = mcp.execute_function("run_local_command", params);
    ASSERT_TRUE(result.success) << "Error: " << result.error_message;
    
    // Clean output (echo adds a newline)
    std::string clean_result = result.result;
    clean_result.erase(clean_result.find_last_not_of(" \t\n\r") + 1);
    EXPECT_EQ(clean_result, "mcp_test_output");
}
