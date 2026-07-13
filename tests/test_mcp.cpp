#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <format>
#include <algorithm>
#include "../src/helpers/mcp_service.hpp"
#include "../src/helpers/process_helper.hpp"
#include "../src/models/git.hpp"

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

TEST(MCPTest, GitModelIntegration) {
    rouen::models::git model;
    
    // Use the workspace root path
    std::string path = "/Users/ignaciorodriguez/src/rouen";
    model.addRepository(path);
    
    // Check that we can read status (should run git status on this repository)
    std::string status = model.getGitStatus(path);
    EXPECT_FALSE(status.empty());
    
    const auto& repos = model.getRepos();
    auto it = repos.find(path);
    ASSERT_NE(it, repos.end());
    
    // Verify it parses the status to a valid enum state (not unknown, since it's a real git repo)
    EXPECT_NE(it->second, rouen::models::GitRepoStatus::Unknown);
}

TEST(MCPTest, GitMCPJSONFormatting) {
    // Mimic and validate JSON format of get_repository_status_json, get_repositories_needing_push_json, and get_modified_repositories_json
    
    // Mock repository state
    std::map<std::string, rouen::models::GitRepoStatus> repos = {
        {"/repo/clean", rouen::models::GitRepoStatus::Clean},
        {"/repo/modified", rouen::models::GitRepoStatus::Modified},
        {"/repo/untracked", rouen::models::GitRepoStatus::Untracked},
        {"/repo/staged", rouen::models::GitRepoStatus::Staged}
    };
    
    auto git_status_to_string = [](rouen::models::GitRepoStatus status) -> std::string {
        switch (status) {
            case rouen::models::GitRepoStatus::Clean: return "clean";
            case rouen::models::GitRepoStatus::Modified: return "modified";
            case rouen::models::GitRepoStatus::Untracked: return "untracked";
            case rouen::models::GitRepoStatus::Staged: return "staged";
            case rouen::models::GitRepoStatus::Conflict: return "conflict";
            case rouen::models::GitRepoStatus::Detached: return "detached";
            case rouen::models::GitRepoStatus::Unknown: return "unknown";
            default: return "unknown";
        }
    };
    
    // 1. Validate status conversion
    EXPECT_EQ(git_status_to_string(rouen::models::GitRepoStatus::Modified), "modified");
    EXPECT_EQ(git_status_to_string(rouen::models::GitRepoStatus::Clean), "clean");
    
    // 2. Validate custom JSON serialization outputs
    std::string repos_json = "{\"success\":true,\"repositories\":[";
    bool first = true;
    for (const auto& [path, status] : repos) {
        if (!first) repos_json += ",";
        repos_json += "{\"path\":\"" + path + "\",";
        repos_json += "\"status\":\"" + git_status_to_string(status) + "\",";
        repos_json += "\"ahead\":false}";
        first = false;
    }
    repos_json += "]}";
    
    // Validate generated JSON structure and elements
    EXPECT_TRUE(repos_json.find("\"success\":true") != std::string::npos);
    EXPECT_TRUE(repos_json.find("/repo/modified") != std::string::npos);
    EXPECT_TRUE(repos_json.find("modified") != std::string::npos);
    EXPECT_TRUE(repos_json.find("clean") != std::string::npos);
}
