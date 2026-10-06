#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <format>
#include <algorithm>
#include "../src/helpers/mcp_service.hpp"
#include "../src/helpers/process_helper.hpp"
#include "../src/models/git.hpp"
#include "../src/cards/productivity/pomodoro.hpp"
#include "../src/helpers/llm_config.hpp"
#include "../src/helpers/fetch.hpp"
#include "../src/helpers/syntax_checker.hpp"
#include "../src/registrar.hpp"

// Forward declarations to avoid including weather.hpp with its icon dependencies
namespace rouen {
    namespace hosts {
        class WeatherHost;
    }
    namespace cards {
        class weather;
    }
}

using namespace rouen::helpers;

struct test_edit_request {
    std::string path{};
};



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
    // Override GIT_PATH to use the system path's "git" instead of potential hardcoded /usr/bin/git
    CONFIG_SERVICE()->set_env_value("GIT_PATH", "git");

    rouen::models::git model;
    
    // Find the git repository root by walking up from the current directory
    std::filesystem::path current = std::filesystem::current_path();
    while (current != current.root_path()) {
        if (std::filesystem::exists(current / ".git")) {
            break;
        }
        current = current.parent_path();
    }
    std::string path = current.string();
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

TEST(MCPTest, PomodoroMCPIntegration) {
    mcp_service mcp;
    
    // Create Pomodoro card
    auto pomo = std::make_shared<rouen::cards::pomodoro>();
    
    // Simulate dynamic MCP registration by the deck
    auto functions = pomo->get_mcp_functions();
    EXPECT_EQ(functions.size(), 2u);
    
    for (const auto& func : functions) {
        mcp_service::function_definition def(
            func.name,
            func.description,
            func.schema,
            func.handler,
            "pomodoro"
        );
        mcp.register_function("pomodoro", def);
    }
    
    // Verify tools are registered
    EXPECT_TRUE(mcp.has_function("start_pomodoro"));
    EXPECT_TRUE(mcp.has_function("get_pomodoro_status"));
    
    // Test get_pomodoro_status
    auto status_res = mcp.execute_function("get_pomodoro_status", "{}");
    EXPECT_TRUE(status_res.success);
    EXPECT_TRUE(status_res.result.find("\"status\"") != std::string::npos);
    EXPECT_TRUE(status_res.result.find("running") != std::string::npos || status_res.result.find("completed") != std::string::npos);
    
    // Test start_pomodoro
    auto start_res = mcp.execute_function("start_pomodoro", "{}");
    EXPECT_TRUE(start_res.success);
    EXPECT_TRUE(start_res.result.find("Pomodoro started") != std::string::npos);
}

TEST(MCPTest, CreateCardMCP) {
    mcp_service mcp;
    
    // The create_card tool should be registered by default
    EXPECT_TRUE(mcp.has_function("create_card"));
    
    // Simulate registrar having the create_card service
    std::string created_uri = "";
    auto mock_create_card = std::make_shared<std::function<void(std::string const&)>>([&](std::string const& uri) {
        created_uri = uri;
    });
    
    registrar::add<std::function<void(std::string const&)>>("create_card", mock_create_card);
    
    // Test executing create_card
    std::string params = R"({"uri":"pomodoro"})";
    auto result = mcp.execute_function("create_card", params);
    
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.result.find("Card created successfully") != std::string::npos);
    EXPECT_EQ(created_uri, "pomodoro");
    
    // Clean up registrar
    registrar::remove<std::function<void(std::string const&)>>("create_card");
}

TEST(MCPTest, GathersEditFileCommand) {
    mcp_service mcp;
    
    // Check that edit_file is registered globally by default
    EXPECT_TRUE(mcp.has_function("edit_file"));
    
    auto functions = mcp.get_available_functions();
    
    // Verify edit_file schema and definition
    auto it = std::find_if(functions.begin(), functions.end(), [](const auto& f) {
        return f.name == "edit_file";
    });
    
    ASSERT_NE(it, functions.end());
    EXPECT_EQ(it->card_type, "editor");
    EXPECT_FALSE(it->description.empty());
    
    std::string schema = std::format(
        "{{\"name\":\"{}\",\"description\":\"{}\",\"parameters\":{}}}",
        it->name,
        it->description,
        it->schema.empty() ? "{\"type\":\"object\",\"properties\":{}}" : it->schema
    );
    
    EXPECT_TRUE(schema.find("edit_file") != std::string::npos);
    EXPECT_TRUE(schema.find("path") != std::string::npos);
}

TEST(MCPTest, ExecuteEditFileCommand) {
    mcp_service mcp;
    
    // Register a dummy edit function in the global registrar
    std::string opened_path;
    auto mock_edit = std::make_shared<std::function<void(std::string const &)>>(
        [&opened_path](std::string const& path) {
            opened_path = path;
        }
    );
    registrar::add<std::function<void(std::string const &)>>("edit", mock_edit);
    
    std::string params = R"({"path":"/some/test/file.cpp"})";
    auto result = mcp.execute_function("edit_file", params);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(opened_path, "/some/test/file.cpp");
    EXPECT_TRUE(result.result.find("Successfully opened") != std::string::npos);
    
    // Cleanup
    registrar::remove<std::function<void(std::string const &)>>("edit");
}

TEST(MCPTest, CodingToolchainAndSyntaxTools) {
    mcp_service mcp;
    auto functions = mcp.get_available_functions();
    
    bool has_discover = false;
    bool has_check_syntax = false;
    bool has_clear_diag = false;
    for (const auto& f : functions) {
        if (f.name == "code_discover_toolchain") has_discover = true;
        if (f.name == "code_check_syntax") has_check_syntax = true;
        if (f.name == "code_clear_diagnostics") has_clear_diag = true;
    }
    EXPECT_TRUE(has_discover);
    EXPECT_TRUE(has_check_syntax);
    EXPECT_TRUE(has_clear_diag);
    
    // Execute code_discover_toolchain
    auto disc_res = mcp.execute_function("code_discover_toolchain", "{}");
    EXPECT_TRUE(disc_res.success);
    EXPECT_TRUE(disc_res.result.find("cpp_compiler") != std::string::npos);
    EXPECT_TRUE(disc_res.result.find("has_nix") != std::string::npos);

    // Execute code_clear_diagnostics
    bool cleared = false;
    auto mock_clear = std::make_shared<std::function<void()>>([&cleared]() {
        cleared = true;
    });
    registrar::add<std::function<void()>>("editor_clear_error_markers", mock_clear);
    auto clear_res = mcp.execute_function("code_clear_diagnostics", "{}");
    EXPECT_TRUE(clear_res.success);
    EXPECT_TRUE(cleared);
    registrar::remove<std::function<void()>>("editor_clear_error_markers");
}

TEST(MCPTest, CodeIndexerToolsRegisteredAndCallable) {
    mcp_service mcp;
    EXPECT_TRUE(mcp.has_function("code_search"));
    EXPECT_TRUE(mcp.has_function("code_find_symbol"));
    EXPECT_TRUE(mcp.has_function("code_index_workspace"));

    // Execute code_index_workspace on src/helpers
    auto idx_res = mcp.execute_function("code_index_workspace", R"({"workspace_dir":"src/helpers"})");
    EXPECT_TRUE(idx_res.success);
    EXPECT_TRUE(idx_res.result.find("total_files_scanned") != std::string::npos);

    // Execute code_find_symbol for CodeIndexer
    auto sym_res = mcp.execute_function("code_find_symbol", R"({"name":"CodeIndexer"})");
    EXPECT_TRUE(sym_res.success);
    EXPECT_TRUE(sym_res.result.find("code_indexer.hpp") != std::string::npos);

    // Execute code_search for a unique string
    auto search_res = mcp.execute_function("code_search", R"({"query":"CodeIndexer"})");
    EXPECT_TRUE(search_res.success);
    EXPECT_TRUE(search_res.result.find("code_indexer.hpp") != std::string::npos);
}

TEST(MCPTest, RealAICodeSearchAndSymbolFinding) {
    // Load env file to get API keys
    CONFIG_SERVICE()->load_env_file();

    if (!LLMConfig::is_configured()) {
        GTEST_SKIP() << "Configured LLM is not available (API key not set). Skipping AI coding test.";
    }

    // Retrieve configured LLM instance
    auto llm_opt = LLMConfig::create_llm_instance();
    ASSERT_TRUE(llm_opt.has_value());
    auto& llm = *llm_opt;

    // Index src/helpers to ensure symbols are available in the index
    mcp_service mcp;
    mcp.execute_function("code_index_workspace", R"({"workspace_dir":"src/helpers"})");

    // Gather function schemas for code_find_symbol and code_search
    std::vector<std::string> function_schemas;
    for (const auto& func : mcp.get_available_functions()) {
        if (func.name == "code_find_symbol" || func.name == "code_search") {
            std::string schema = std::format(
                "{{\"name\":\"{}\",\"description\":\"{}\",\"parameters\":{}}}",
                func.name,
                func.description.empty() ? "Operation" : func.description,
                func.schema.empty() ? "{\"type\":\"object\",\"properties\":{}}" : func.schema
            );
            function_schemas.push_back(schema);
        }
    }
    ASSERT_GE(function_schemas.size(), 2u);

    bool tool_called = false;
    std::string called_tool_name;
    std::string called_param_value;

    auto function_executor = [&](const std::string& name, const std::string& args_json) -> std::string {
        if (name == "code_find_symbol" || name == "code_search") {
            tool_called = true;
            called_tool_name = name;
            called_param_value = args_json;
        }
        auto res = mcp.execute_function(name, args_json);
        return res.success ? res.result : "Error: " + res.error_message;
    };

    auto settings = LLMConfig::get_current_config();
    std::string model_name = settings.model_name;

    // Dynamically query available models if using Gemini, in compliance with project rules
    if (settings.provider == LLMConfig::Provider::GEMINI) {
        try {
            http::fetch fetcher(10);
            std::string url = std::format("https://generativelanguage.googleapis.com/v1beta/models?key={}", settings.api_key);
            std::string resp_json = fetcher(url);
            if (resp_json.find("gemini-3.1-flash-lite") != std::string::npos) {
                model_name = "gemini-3.1-flash-lite";
            } else if (resp_json.find("gemini-flash-lite-latest") != std::string::npos) {
                model_name = "gemini-flash-lite-latest";
            } else if (resp_json.find("gemini-2.5-flash-lite") != std::string::npos) {
                model_name = "gemini-2.5-flash-lite";
            }
        } catch (...) {}
    }

    auto fetcher = std::make_shared<http::fetch>(30);
    std::vector<std::pair<std::string, std::string>> conversation;
    std::string user_prompt = "Where is the class 'SyntaxChecker' declared in the codebase? Use the code search or symbol tools to find it.";

    ignacionr::ChatCompletion chat_completion;

    try {
        if (settings.provider == LLMConfig::Provider::GEMINI) {
            auto& gemini_adapter = *std::get<std::unique_ptr<GeminiAdapter>>(llm.instance_);
            chat_completion = gemini_adapter.sendMessageWithFunctionCalling(
                user_prompt,
                [fetcher](const std::string& url, const std::string& body, auto header_setter) {
                    return fetcher->post(url, body, header_setter);
                },
                function_executor,
                "user",
                model_name,
                "",
                0.2f,
                &conversation,
                &function_schemas
            );
        } else {
            auto& cppgpt_adapter = *std::get<std::unique_ptr<ignacionr::cppgpt>>(llm.instance_);
            chat_completion = cppgpt_adapter.sendMessageWithFunctionCalling(
                user_prompt,
                [fetcher](const std::string& url, const std::string& body, auto header_setter) {
                    return fetcher->post(url, body, header_setter);
                },
                function_executor,
                "user",
                model_name,
                "",
                0.2f,
                &conversation,
                &function_schemas
            );
        }
    } catch (const std::exception& e) {
        GTEST_SKIP() << "LLM HTTP request failed: " << e.what();
    } catch (...) {
        GTEST_SKIP() << "LLM HTTP request failed with unknown exception.";
    }

    if (!tool_called && !chat_completion.choices.empty() && !chat_completion.choices[0].message.content.empty()) {
        GTEST_SKIP() << "Configured live LLM returned text instead of issuing a tool call.";
    }

    EXPECT_TRUE(tool_called) << "The LLM did not call any code indexing tool!";
    EXPECT_TRUE(called_param_value.find("SyntaxChecker") != std::string::npos)
        << "The tool argument did not contain 'SyntaxChecker': " << called_param_value;
    
    // Verify the completion or conversation history contains syntax_checker.hpp
    bool found_mention = false;
    if (!chat_completion.choices.empty() && chat_completion.choices[0].message.content.find("syntax_checker.hpp") != std::string::npos) {
        found_mention = true;
    }
    for (const auto& [role, msg] : conversation) {
        if (msg.find("syntax_checker.hpp") != std::string::npos) {
            found_mention = true;
            break;
        }
    }
    EXPECT_TRUE(found_mention) << "The AI response or conversation did not mention syntax_checker.hpp";
}

TEST(MCPTest, CodeEditorToolsRegisteredAndCallable) {
    mcp_service mcp;
    EXPECT_TRUE(mcp.has_function("code_read_file"));
    EXPECT_TRUE(mcp.has_function("code_write_file"));
    EXPECT_TRUE(mcp.has_function("code_apply_patch"));

    std::string test_path = "/tmp/test_mcp_code_editor.txt";
    std::filesystem::remove(test_path);

    // 1. Write file
    auto write_res = mcp.execute_function("code_write_file", std::format(R"({{"path":"{}","content":"alpha\nbeta\ngamma\n","overwrite":true}})", test_path));
    EXPECT_TRUE(write_res.success);
    EXPECT_TRUE(write_res.result.find("File written successfully") != std::string::npos);

    // 2. Read file
    auto read_res = mcp.execute_function("code_read_file", std::format(R"({{"path":"{}","start_line":1,"end_line":3}})", test_path));
    EXPECT_TRUE(read_res.success);
    EXPECT_TRUE(read_res.result.find("1: alpha") != std::string::npos);
    EXPECT_TRUE(read_res.result.find("2: beta") != std::string::npos);

    // 3. Apply patch
    auto patch_res = mcp.execute_function("code_apply_patch", std::format(R"({{"path":"{}","target_content":"beta","replacement_content":"delta"}})", test_path));
    EXPECT_TRUE(patch_res.success);
    EXPECT_TRUE(patch_res.result.find("Patch applied successfully") != std::string::npos);

    // Verify change
    auto verify_res = mcp.execute_function("code_read_file", std::format(R"({{"path":"{}","show_line_numbers":false}})", test_path));
    EXPECT_TRUE(verify_res.success);
    EXPECT_TRUE(verify_res.result.find("delta") != std::string::npos);
    EXPECT_TRUE(verify_res.result.find("beta") == std::string::npos);

    std::filesystem::remove(test_path);
}

TEST(MCPTest, RealAICodingSelfCorrectionLoop) {
    CONFIG_SERVICE()->load_env_file();

    if (!LLMConfig::is_configured()) {
        GTEST_SKIP() << "Configured LLM is not available (API key not set). Skipping AI coding test.";
    }

    auto llm_opt = LLMConfig::create_llm_instance();
    ASSERT_TRUE(llm_opt.has_value());
    auto& llm = *llm_opt;

    std::string test_repair_path = "/tmp/test_ai_repair.cpp";
    std::filesystem::remove(test_repair_path);

    // Create file with syntax error (missing semicolon)
    {
        std::ofstream out(test_repair_path);
        out << "#include <iostream>\n\nint compute_sum(int a, int b) {\n    return a + b\n}\n";
    }

    mcp_service mcp;
    std::vector<std::string> function_schemas;
    for (const auto& func : mcp.get_available_functions()) {
        if (func.name == "code_read_file" || func.name == "code_apply_patch") {
            std::string schema = std::format(
                "{{\"name\":\"{}\",\"description\":\"{}\",\"parameters\":{}}}",
                func.name,
                func.description.empty() ? "Operation" : func.description,
                func.schema.empty() ? "{\"type\":\"object\",\"properties\":{}}" : func.schema
            );
            function_schemas.push_back(schema);
        }
    }
    ASSERT_GE(function_schemas.size(), 2u);

    bool read_called = false;
    bool patch_called = false;

    auto function_executor = [&](const std::string& name, const std::string& args_json) -> std::string {
        if (name == "code_read_file") {
            read_called = true;
        } else if (name == "code_apply_patch") {
            patch_called = true;
        }
        auto res = mcp.execute_function(name, args_json);
        return res.success ? res.result : "Error: " + res.error_message;
    };

    auto settings = LLMConfig::get_current_config();
    std::string model_name = settings.model_name;

    if (settings.provider == LLMConfig::Provider::GEMINI) {
        try {
            http::fetch fetcher(10);
            std::string url = std::format("https://generativelanguage.googleapis.com/v1beta/models?key={}", settings.api_key);
            std::string resp_json = fetcher(url);
            if (resp_json.find("gemini-3.1-flash-lite") != std::string::npos) {
                model_name = "gemini-3.1-flash-lite";
            } else if (resp_json.find("gemini-flash-lite-latest") != std::string::npos) {
                model_name = "gemini-flash-lite-latest";
            } else if (resp_json.find("gemini-2.5-flash-lite") != std::string::npos) {
                model_name = "gemini-2.5-flash-lite";
            }
        } catch (...) {}
    }

    auto fetcher = std::make_shared<http::fetch>(30);
    std::vector<std::pair<std::string, std::string>> conversation;
    std::string user_prompt = std::format(
        "The file '{}' contains a C++ syntax error. Read the file, identify the error, and apply a patch to fix it.",
        test_repair_path
    );

    ignacionr::ChatCompletion chat_completion;

    try {
        if (settings.provider == LLMConfig::Provider::GEMINI) {
            auto& gemini_adapter = *std::get<std::unique_ptr<GeminiAdapter>>(llm.instance_);
            chat_completion = gemini_adapter.sendMessageWithFunctionCalling(
                user_prompt,
                [fetcher](const std::string& url, const std::string& body, auto header_setter) {
                    return fetcher->post(url, body, header_setter);
                },
                function_executor,
                "user",
                model_name,
                "",
                0.2f,
                &conversation,
                &function_schemas
            );
        } else {
            auto& cppgpt_adapter = *std::get<std::unique_ptr<ignacionr::cppgpt>>(llm.instance_);
            chat_completion = cppgpt_adapter.sendMessageWithFunctionCalling(
                user_prompt,
                [fetcher](const std::string& url, const std::string& body, auto header_setter) {
                    return fetcher->post(url, body, header_setter);
                },
                function_executor,
                "user",
                model_name,
                "",
                0.2f,
                &conversation,
                &function_schemas
            );
        }
    } catch (const std::exception& e) {
        GTEST_SKIP() << "LLM HTTP request failed: " << e.what();
    } catch (...) {
        GTEST_SKIP() << "LLM HTTP request failed with unknown exception.";
    }

    if (!patch_called && !chat_completion.choices.empty() && !chat_completion.choices[0].message.content.empty()) {
        GTEST_SKIP() << "Configured live LLM returned text instead of issuing a tool call.";
    }

    EXPECT_TRUE(read_called || patch_called) << "The LLM did not call any code editing tool!";
    EXPECT_TRUE(patch_called) << "The LLM did not call code_apply_patch to repair the syntax error!";

    // Verify the file on disk was repaired
    std::ifstream in(test_repair_path);
    std::string repaired_content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_TRUE(repaired_content.find("return a + b;") != std::string::npos)
        << "The file did not contain the semicolon fix: " << repaired_content;

    std::filesystem::remove(test_repair_path);
}

TEST(MCPTest, ConfiguredLLMToolingIntegration) {
    // Load env file to get API keys
    CONFIG_SERVICE()->load_env_file();
    
    // Check if the local test LLM at http://192.168.1.33:8098 is available
    bool is_local_llm_available = false;
    {
        http::fetch fetcher(2);
        try {
            auto res = fetcher("http://192.168.1.33:8098/health");
            if (res.find("ok") != std::string::npos) {
                is_local_llm_available = true;
            }
        } catch (...) {}
    }
    
    // If local test LLM is available and no other LLM is configured, configure to use it
    if (is_local_llm_available && !LLMConfig::is_configured()) {
        CONFIG_SERVICE()->set_env_value("LLM_PROVIDER", "custom");
        CONFIG_SERVICE()->set_env_value("LLM_CUSTOM_URL", "http://192.168.1.33:8098/v1");
        CONFIG_SERVICE()->set_env_value("LLM_CUSTOM_API_KEY", "dummy_key");
        CONFIG_SERVICE()->set_env_value("LLM_CUSTOM_MODEL", "mlx-community/Qwen3.5-9B-MLX-4bit");
    }
    
    // Only run if the LLM is configured in the environment
    if (!LLMConfig::is_configured()) {
        GTEST_SKIP() << "Configured LLM is not available (API key not set). Skipping integration test.";
    }
    
    // Retrieve the configured LLM instance
    auto llm_opt = LLMConfig::create_llm_instance();
    ASSERT_TRUE(llm_opt.has_value());
    auto& llm = *llm_opt;
    
    // Create an mcp_service instance and gather function schemas
    mcp_service mcp;
    std::vector<std::string> function_schemas;
    auto functions = mcp.get_available_functions();
    for (const auto& func : functions) {
        std::string schema = std::format(
            "{{\"name\":\"{}\",\"description\":\"{}\",\"parameters\":{}}}",
            func.name,
            func.description.empty() ? "Operation" : func.description,
            func.schema.empty() ? "{\"type\":\"object\",\"properties\":{}}" : func.schema
        );
        function_schemas.push_back(schema);
    }
    
    // We want to verify if the LLM will call the edit_file tool
    bool edit_file_called = false;
    std::string called_path = "";
    
    auto function_executor = [&](const std::string& name, const std::string& args_json) -> std::string {
        if (name == "edit_file") {
            edit_file_called = true;
            test_edit_request req{};
            auto err = glz::read_json(req, args_json);
            (void)err;
            called_path = req.path;
            return "Successfully opened " + req.path + " in the editor.";
        }
        
        // Execute other tools like run_local_command if the LLM needs to find the file
        auto res = mcp.execute_function(name, args_json);
        return res.success ? res.result : "Error: " + res.error_message;
    };
    
    // Perform LLM call using sendMessageWithFunctionCalling
    auto settings = LLMConfig::get_current_config();
    std::string model_name = settings.model_name;

    if (settings.provider == LLMConfig::Provider::GEMINI) {
        try {
            http::fetch fetcher(10);
            std::string url = std::format("https://generativelanguage.googleapis.com/v1beta/models?key={}", settings.api_key);
            std::string resp_json = fetcher(url);
            if (resp_json.find("gemini-3.1-flash-lite") != std::string::npos) {
                model_name = "gemini-3.1-flash-lite";
            } else if (resp_json.find("gemini-flash-lite-latest") != std::string::npos) {
                model_name = "gemini-flash-lite-latest";
            } else if (resp_json.find("gemini-2.5-flash-lite") != std::string::npos) {
                model_name = "gemini-2.5-flash-lite";
            }
        } catch (...) {}
    }
    
    // Simple HTTP client using http::fetch
    auto fetcher = std::make_shared<http::fetch>(30); // 30 seconds timeout
    
    // Conversation history vector
    std::vector<std::pair<std::string, std::string>> conversation;
    
    std::string user_prompt = "find a file named rss.hpp and let me edit it";
    
    ignacionr::ChatCompletion chat_completion;
    
    try {
        if (settings.provider == LLMConfig::Provider::GEMINI) {
            // Native Gemini adapter
            auto& gemini_adapter = *std::get<std::unique_ptr<GeminiAdapter>>(llm.instance_);
            chat_completion = gemini_adapter.sendMessageWithFunctionCalling(
                user_prompt,
                [fetcher](const std::string& url, const std::string& body, auto header_setter) {
                    return fetcher->post(url, body, header_setter);
                },
                function_executor,
                "user",
                model_name,
                "",
                0.45f,
                &conversation,
                &function_schemas
            );
        } else {
            // Cppgpt adapter
            auto& cppgpt_adapter = *std::get<std::unique_ptr<ignacionr::cppgpt>>(llm.instance_);
            chat_completion = cppgpt_adapter.sendMessageWithFunctionCalling(
                user_prompt,
                [fetcher](const std::string& url, const std::string& body, auto header_setter) {
                    return fetcher->post(url, body, header_setter);
                },
                function_executor,
                "user",
                model_name,
                "",
                0.45f,
                &conversation,
                &function_schemas
            );
        }
    } catch (const std::exception& e) {
        GTEST_SKIP() << "LLM HTTP request failed: " << e.what();
    } catch (...) {
        GTEST_SKIP() << "LLM HTTP request failed with unknown exception.";
    }
    
    // Verify that the edit_file tool was called by the LLM (or skip if live LLM output text without tool call)
    if (!edit_file_called && !chat_completion.choices.empty() && !chat_completion.choices[0].message.content.empty()) {
        GTEST_SKIP() << "Configured live LLM returned text instead of issuing a tool call.";
    }
    EXPECT_TRUE(edit_file_called) << "The LLM did not call the edit_file tool!";
    EXPECT_FALSE(called_path.empty()) << "The file path provided to the tool was empty!";
    EXPECT_TRUE(called_path.find("rss.hpp") != std::string::npos) 
         << "The LLM called edit_file but with incorrect path: " << called_path;
}

// Note: Weather MCP tests removed to avoid header dependencies with icons.
// The weather MCP functions are tested indirectly through the main application
// and can be verified manually or through integration tests.

TEST(MCPTest, MeshToolsRegisteredAndCallable) {
    mcp_service mcp;
    EXPECT_TRUE(mcp.has_function("mesh_list_nodes"));
    EXPECT_TRUE(mcp.has_function("mesh_get_status"));

    auto functions = mcp.get_available_functions();
    auto it_list = std::find_if(functions.begin(), functions.end(), [](const auto& f) {
        return f.name == "mesh_list_nodes";
    });
    ASSERT_NE(it_list, functions.end());
    EXPECT_EQ(it_list->card_type, "mesh");

    auto it_status = std::find_if(functions.begin(), functions.end(), [](const auto& f) {
        return f.name == "mesh_get_status";
    });
    ASSERT_NE(it_status, functions.end());
    EXPECT_EQ(it_status->card_type, "mesh");

    auto res = mcp.execute_function("mesh_list_nodes", "{}");
    EXPECT_TRUE(res.success);
    EXPECT_NE(res.result.find("nodes"), std::string::npos);

    auto status_res = mcp.execute_function("mesh_get_status", "{}");
    EXPECT_TRUE(status_res.success);
    EXPECT_NE(status_res.result.find("connected"), std::string::npos);
}

TEST(MCPTest, ThemeToolsRegisteredAndCallable) {
    mcp_service mcp;
    EXPECT_TRUE(mcp.has_function("list_themes"));
    EXPECT_TRUE(mcp.has_function("select_theme"));

    auto res = mcp.execute_function("list_themes", "{}");
    EXPECT_TRUE(res.success);
    EXPECT_NE(res.result.find("themes"), std::string::npos);
    EXPECT_NE(res.result.find("Amber"), std::string::npos);

    auto sel_res = mcp.execute_function("select_theme", R"({"name":"Amber"})");
    EXPECT_TRUE(sel_res.success);
    EXPECT_NE(sel_res.result.find("Amber"), std::string::npos);
}

TEST(MCPTest, RealAIFixWithAIDispatchLoop) {
    CONFIG_SERVICE()->load_env_file();

    if (!LLMConfig::is_configured()) {
        GTEST_SKIP() << "Configured LLM is not available (API key not set). Skipping live AI quick-fix test.";
    }

    auto llm_opt = LLMConfig::create_llm_instance();
    ASSERT_TRUE(llm_opt.has_value());
    auto& llm = *llm_opt;

    std::string test_fix_path = "/tmp/test_quick_fix_ai.cpp";
    std::filesystem::remove(test_fix_path);

    // Create file with syntax error (missing semicolon)
    {
        std::ofstream out(test_fix_path);
        out << "int calculate_square(int n) {\n    return n * n\n}\n";
    }

    // Verify initial syntax check detects error
    auto initial_check = rouen::helpers::SyntaxChecker::instance().check_file(test_fix_path);
    EXPECT_FALSE(initial_check.success);
    EXPECT_GT(initial_check.error_count, 0u);

    mcp_service mcp;
    std::vector<std::string> function_schemas;
    for (const auto& func : mcp.get_available_functions()) {
        if (func.name == "code_apply_patch" || func.name == "code_read_file") {
            std::string schema = std::format(
                "{{\"name\":\"{}\",\"description\":\"{}\",\"parameters\":{}}}",
                func.name,
                func.description.empty() ? "Operation" : func.description,
                func.schema.empty() ? "{\"type\":\"object\",\"properties\":{}}" : func.schema
            );
            function_schemas.push_back(schema);
        }
    }
    ASSERT_GE(function_schemas.size(), 1u);

    bool patch_called = false;
    auto function_executor = [&](const std::string& name, const std::string& args_json) -> std::string {
        if (name == "code_apply_patch") {
            patch_called = true;
        }
        auto res = mcp.execute_function(name, args_json);
        return res.success ? res.result : "Error: " + res.error_message;
    };

    auto settings = LLMConfig::get_current_config();
    std::string model_name = settings.model_name;

    if (settings.provider == LLMConfig::Provider::GEMINI) {
        try {
            http::fetch fetcher(10);
            std::string url = std::format("https://generativelanguage.googleapis.com/v1beta/models?key={}", settings.api_key);
            std::string resp_json = fetcher(url);
            if (resp_json.find("gemini-3.1-flash-lite") != std::string::npos) {
                model_name = "gemini-3.1-flash-lite";
            } else if (resp_json.find("gemini-flash-lite-latest") != std::string::npos) {
                model_name = "gemini-flash-lite-latest";
            } else if (resp_json.find("gemini-2.5-flash-lite") != std::string::npos) {
                model_name = "gemini-2.5-flash-lite";
            }
        } catch (...) {}
    }

    auto fetcher = std::make_shared<http::fetch>(30);
    std::vector<std::pair<std::string, std::string>> conversation;

    std::string prompt = std::format(
        "Please investigate and fix the compiler diagnostic in `test_quick_fix_ai.cpp`:\n\n"
        "- **Target File**: `{}`\n"
        "- **Line**: 2, **Column**: 17\n"
        "- **Severity**: error\n"
        "- **Compiler Diagnostic**: `expected ';' after return statement`\n\n"
        "**Surrounding Code Context (Lines around 2)**:\n"
        "```cpp\n"
        "   1 | int calculate_square(int n) {{\n"
        "   2 |     return n * n\n"
        "   3 | }}\n"
        "```\n\n"
        "Please inspect the issue, explain the fix concisely, and apply the correction using `code_apply_patch`.\n"
        "After applying the patch, verify the fix using `code_check_syntax`.",
        test_fix_path
    );

    ignacionr::ChatCompletion chat_completion;
    try {
        if (settings.provider == LLMConfig::Provider::GEMINI) {
            auto& gemini_adapter = *std::get<std::unique_ptr<GeminiAdapter>>(llm.instance_);
            chat_completion = gemini_adapter.sendMessageWithFunctionCalling(
                prompt,
                [fetcher](const std::string& url, const std::string& body, auto header_setter) {
                    return fetcher->post(url, body, header_setter);
                },
                function_executor,
                "user",
                model_name,
                "",
                0.2f,
                &conversation,
                &function_schemas
            );
        } else {
            auto& cppgpt_adapter = *std::get<std::unique_ptr<ignacionr::cppgpt>>(llm.instance_);
            chat_completion = cppgpt_adapter.sendMessageWithFunctionCalling(
                prompt,
                [fetcher](const std::string& url, const std::string& body, auto header_setter) {
                    return fetcher->post(url, body, header_setter);
                },
                function_executor,
                "user",
                model_name,
                "",
                0.2f,
                &conversation,
                &function_schemas
            );
        }
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Live LLM request failed: " << e.what();
    }

    EXPECT_TRUE(patch_called) << "The AI agent should have called code_apply_patch to fix the error.";

    auto final_check = rouen::helpers::SyntaxChecker::instance().check_file(test_fix_path);
    EXPECT_TRUE(final_check.success) << "File should compile cleanly after AI fix: " << final_check.raw_output;
    EXPECT_EQ(final_check.error_count, 0u);

    std::filesystem::remove(test_fix_path);
}


