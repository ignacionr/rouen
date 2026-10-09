/**
 * Test: GeminiAdapterTest
 * Purpose: Validates that the GeminiAdapter correctly serializes conversations, 
 *          function calls, and function responses into Gemini API request payloads.
 * Category: Feature
 */

// 1. Standard headers first to prevent macro pollution
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <ranges>

// 2. GTest and dependency headers
#include <gtest/gtest.h>

// 3. Glaze and project headers before macro definitions
#include "../src/helpers/glaze_include.hpp"
#include "../src/helpers/config_service.hpp"
#include "../src/helpers/process_helper.hpp"

// 4. Define private public with warning suppression for whitebox test access
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif

#define private public

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "../src/helpers/gemini_adapter.hpp"

using namespace rouen::helpers;

TEST(GeminiAdapterTest, SerializesTextMessageCorrectly) {
    GeminiAdapter adapter("dummy_api_key");
    std::vector<GeminiAdapter::Message> conversation;
    conversation.emplace_back("user", "Hello Gemini");
    
    std::string request = adapter.build_gemini_request(conversation, 0.5f, false);
    
    // Parse the generated JSON back to verify its structure
    glz::json_t doc;
    auto err = glz::read_json(doc, request);
    ASSERT_FALSE(err) << glz::format_error(err, request);
    
    ASSERT_TRUE(doc.contains("contents"));
    auto contents = doc["contents"];
    ASSERT_EQ(contents.size(), 1u);
    
    auto first_msg = contents[0];
    EXPECT_EQ(first_msg["role"].get<std::string>(), "user");
    
    auto parts = first_msg["parts"];
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0]["text"].get<std::string>(), "Hello Gemini");
}

TEST(GeminiAdapterTest, SerializesGenerationConfigWithoutTemperature) {
    GeminiAdapter adapter("dummy_api_key");
    std::vector<GeminiAdapter::Message> conversation;
    conversation.emplace_back("user", "Hello Gemini");
    
    std::string request = adapter.build_gemini_request(conversation, 0.5f, false);
    
    glz::json_t doc;
    auto err = glz::read_json(doc, request);
    ASSERT_FALSE(err) << glz::format_error(err, request);
    
    ASSERT_TRUE(doc.contains("generationConfig"));
    auto gen_config = doc["generationConfig"];
    EXPECT_TRUE(gen_config.contains("maxOutputTokens"));
    EXPECT_EQ(gen_config["maxOutputTokens"].get<double>(), 4096.0);
    
    // Ensure deprecated parameters are completely omitted
    EXPECT_FALSE(gen_config.contains("temperature"));
    EXPECT_FALSE(gen_config.contains("top_p"));
    EXPECT_FALSE(gen_config.contains("top_k"));
    EXPECT_FALSE(gen_config.contains("thinking_budget"));
}

TEST(GeminiAdapterTest, SerializesThinkingLevelWhenSpecified) {
    GeminiAdapter adapter("dummy_api_key");
    std::vector<GeminiAdapter::Message> conversation;
    conversation.emplace_back("user", "Hello Gemini");
    
    // When thinking_level is set on adapter
    adapter.set_thinking_level("high");
    EXPECT_EQ(adapter.get_thinking_level(), "high");
    std::string request_high = adapter.build_gemini_request(conversation, 0.5f, false);
    
    glz::json_t doc_high;
    auto err_high = glz::read_json(doc_high, request_high);
    ASSERT_FALSE(err_high) << glz::format_error(err_high, request_high);
    ASSERT_TRUE(doc_high.contains("generationConfig"));
    EXPECT_TRUE(doc_high["generationConfig"].contains("thinkingConfig"));
    EXPECT_EQ(doc_high["generationConfig"]["thinkingConfig"]["thinkingBudget"].get<double>(), 4096.0);
    EXPECT_FALSE(doc_high["generationConfig"].contains("thinking_level"));
    EXPECT_FALSE(doc_high["generationConfig"].contains("temperature"));

    // When thinking_level is explicitly passed to build_gemini_request
    std::string request_override = adapter.build_gemini_request(conversation, 0.5f, false, "minimal");
    glz::json_t doc_override;
    auto err_override = glz::read_json(doc_override, request_override);
    ASSERT_FALSE(err_override) << glz::format_error(err_override, request_override);
    EXPECT_TRUE(doc_override["generationConfig"].contains("thinkingConfig"));
    EXPECT_EQ(doc_override["generationConfig"]["thinkingConfig"]["thinkingBudget"].get<double>(), 512.0);

    // When thinking_level is cleared/empty
    adapter.set_thinking_level("");
    std::string request_empty = adapter.build_gemini_request(conversation, 0.5f, false);
    glz::json_t doc_empty;
    auto err_empty = glz::read_json(doc_empty, request_empty);
    ASSERT_FALSE(err_empty) << glz::format_error(err_empty, request_empty);
    EXPECT_FALSE(doc_empty["generationConfig"].contains("thinkingConfig"));
    EXPECT_FALSE(doc_empty["generationConfig"].contains("thinking_level"));
}

TEST(GeminiAdapterTest, SerializesFunctionCallCorrectly) {
    GeminiAdapter adapter("dummy_api_key");
    std::vector<GeminiAdapter::Message> conversation;
    
    GeminiAdapter::Message msg;
    msg.role = "model";
    msg.content = "I need to run a command.";
    msg.function_calls.push_back({"run_local_command", "{\"command\":\"git status\"}", "", ""});
    conversation.push_back(msg);
    
    std::string request = adapter.build_gemini_request(conversation, 0.5f, false);
    
    glz::json_t doc;
    auto err = glz::read_json(doc, request);
    ASSERT_FALSE(err) << glz::format_error(err, request);
    
    auto contents = doc["contents"];
    ASSERT_EQ(contents.size(), 1u);
    
    auto first_msg = contents[0];
    EXPECT_EQ(first_msg["role"].get<std::string>(), "model");
    
    auto parts = first_msg["parts"];
    ASSERT_EQ(parts.size(), 2u); // One text part, one functionCall part
    
    EXPECT_EQ(parts[0]["text"].get<std::string>(), "I need to run a command.");
    
    auto func_call = parts[1]["functionCall"];
    EXPECT_EQ(func_call["name"].get<std::string>(), "run_local_command");
    
    auto args = func_call["args"];
    EXPECT_EQ(args["command"].get<std::string>(), "git status");
}

TEST(GeminiAdapterTest, SerializesFunctionResponseCorrectly) {
    GeminiAdapter adapter("dummy_api_key");
    std::vector<GeminiAdapter::Message> conversation;
    
    GeminiAdapter::Message msg;
    msg.role = "function";
    msg.function_responses.push_back({"run_local_command", "{\"success\":true,\"output\":\"on branch main\"}"});
    conversation.push_back(msg);
    
    std::string request = adapter.build_gemini_request(conversation, 0.5f, false);
    
    glz::json_t doc;
    auto err = glz::read_json(doc, request);
    ASSERT_FALSE(err) << glz::format_error(err, request);
    
    auto contents = doc["contents"];
    ASSERT_EQ(contents.size(), 1u);
    
    auto first_msg = contents[0];
    EXPECT_EQ(first_msg["role"].get<std::string>(), "user"); // function/tool maps to role "user"
    
    auto parts = first_msg["parts"];
    ASSERT_EQ(parts.size(), 1u);
    
    auto func_resp = parts[0]["functionResponse"];
    EXPECT_EQ(func_resp["name"].get<std::string>(), "run_local_command");
    
    auto response_val = func_resp["response"];
    EXPECT_TRUE(response_val["success"].get<bool>());
    EXPECT_EQ(response_val["output"].get<std::string>(), "on branch main");
}

TEST(GeminiAdapterTest, SerializesRawFunctionResponseCorrectly) {
    GeminiAdapter adapter("dummy_api_key");
    std::vector<GeminiAdapter::Message> conversation;
    
    GeminiAdapter::Message msg;
    msg.role = "function";
    msg.function_responses.push_back({"run_local_command", "some raw non-json text response"});
    conversation.push_back(msg);
    
    std::string request = adapter.build_gemini_request(conversation, 0.5f, false);
    
    glz::json_t doc;
    auto err = glz::read_json(doc, request);
    ASSERT_FALSE(err) << glz::format_error(err, request);
    
    auto contents = doc["contents"];
    auto first_msg = contents[0];
    auto parts = first_msg["parts"];
    auto func_resp = parts[0]["functionResponse"];
    
    EXPECT_EQ(func_resp["name"].get<std::string>(), "run_local_command");
    EXPECT_EQ(func_resp["response"]["result"].get<std::string>(), "some raw non-json text response");
}

#include "../src/helpers/cppgpt.hpp"

TEST(CppGptTest, ParsesToolCallsCorrectly) {
    ignacionr::cppgpt llm("key", "http://127.0.0.1:8098");
    std::vector<std::pair<std::string, std::string>> conversation;
    conversation.push_back({"user", "using the local system, find the current date and time"});

    std::vector<std::string> function_schemas = {
        "{\"name\":\"run_local_command\",\"description\":\"run command\",\"parameters\":{\"type\":\"object\"}}"
    };

    auto mock_executor = [](const std::string& name, const std::string& args) -> std::string {
        EXPECT_EQ(name, "run_local_command");
        EXPECT_EQ(args, "{\"command\": \"date\"}");
        return "Mon Jul 12 15:22:00 UTC 2026";
    };

    int post_count = 0;
    auto mock_post_stateful = [&](const std::string&, const std::string&, auto) -> std::string {
        post_count++;
        if (post_count == 1) {
            return R"({
               "choices" : [
                  {
                     "finish_reason" : "tool_calls",
                     "index" : 0,
                     "message" : {
                        "content" : null,
                        "role" : "assistant",
                        "tool_calls" : [
                           {
                              "function" : {
                                 "arguments" : "{\"command\": \"date\"}",
                                 "name" : "run_local_command"
                              },
                              "id" : "p2CwARQ3zrdiE7a3OzpaMGruupacaf1R",
                              "type" : "function"
                           }
                        ]
                     }
                  }
               ]
            })";
        } else {
            return R"({
               "choices" : [
                  {
                     "finish_reason" : "stop",
                     "index" : 0,
                     "message" : {
                        "content" : "The date is Mon Jul 12 15:22:00 UTC 2026",
                        "role" : "assistant"
                     }
                  }
               ]
            })";
        }
    };

    auto result = llm.sendMessageWithFunctionCalling(
        "using the local system, find the current date and time",
        mock_post_stateful,
        mock_executor,
        "user",
        "qwen",
        "",
        0.1f,
        &conversation,
        &function_schemas
    );

    EXPECT_EQ(result.choices.size(), 1u);
    EXPECT_EQ(result.choices[0].message.content, "The date is Mon Jul 12 15:22:00 UTC 2026");
}

TEST(CppGptTest, MergesSystemInstructionsCorrectly) {
    ignacionr::cppgpt llm("key", "http://127.0.0.1:8098");
    
    // Add multiple system instructions
    llm.add_instructions("System instruction part 1");
    llm.add_instructions("System instruction part 2");
    
    std::vector<std::pair<std::string, std::string>> conversation;
    conversation.push_back({"user", "hello"});
    
    // We mock the post call to capture the generated request body
    std::string captured_body;
    auto mock_post = [&](const std::string&, const std::string& body, auto) -> std::string {
        captured_body = body;
        return R"({
           "choices" : [
              {
                 "finish_reason" : "stop",
                 "index" : 0,
                 "message" : {
                    "content" : "hi",
                    "role" : "assistant"
                 }
              }
           ]
        })";
    };
    
    llm.sendMessage(
        "hello",
        mock_post,
        "user",
        "qwen",
        "",
        0.5f,
        &conversation
    );
    
    // Parse the captured JSON to verify system content was merged
    glz::json_t doc;
    auto err = glz::read_json(doc, captured_body);
    ASSERT_FALSE(err) << glz::format_error(err, captured_body);
    
    ASSERT_TRUE(doc.contains("messages"));
    auto messages = doc["messages"];
    ASSERT_GE(messages.size(), 1u);
    
    // The first message must be the merged system message
    auto first_msg = messages[0];
    EXPECT_EQ(first_msg["role"].get<std::string>(), "system");
    EXPECT_EQ(first_msg["content"].get<std::string>(), "System instruction part 1\n\nSystem instruction part 2");
}

TEST(CppGptTest, MergesSystemInstructionsInFunctionCallingCorrectly) {
    ignacionr::cppgpt llm("key", "http://127.0.0.1:8098");
    
    llm.add_instructions("System prompt instruction 1");
    llm.add_instructions("System prompt instruction 2");
    
    std::vector<std::pair<std::string, std::string>> conversation;
    conversation.push_back({"user", "hello"});
    
    std::vector<std::string> function_schemas = {};
    
    std::string captured_body;
    auto mock_post = [&](const std::string&, const std::string& body, auto) -> std::string {
        captured_body = body;
        return R"({
           "choices" : [
              {
                 "finish_reason" : "stop",
                 "index" : 0,
                 "message" : {
                    "content" : "hi",
                    "role" : "assistant"
                 }
              }
           ]
        })";
    };
    
    auto mock_executor = [](const std::string&, const std::string&) -> std::string {
        return "";
    };
    
    llm.sendMessageWithFunctionCalling(
        "hello",
        mock_post,
        mock_executor,
        "user",
        "qwen",
        "",
        0.5f,
        &conversation,
        &function_schemas
    );
    
    glz::json_t doc;
    auto err = glz::read_json(doc, captured_body);
    ASSERT_FALSE(err) << glz::format_error(err, captured_body);
    
    ASSERT_TRUE(doc.contains("messages"));
    auto messages = doc["messages"];
    ASSERT_GE(messages.size(), 1u);
    
    auto first_msg = messages[0];
    EXPECT_EQ(first_msg["role"].get<std::string>(), "system");
    EXPECT_EQ(first_msg["content"].get<std::string>(), "System prompt instruction 1\n\nSystem prompt instruction 2");
}

TEST(GeminiAdapterTest, FallsBackOnQuotaExhausted) {
    GeminiAdapter adapter("test_key");
    std::vector<std::string> requested_urls;
    auto mock_post = [&](const std::string& url, const std::string&, auto) -> std::string {
        requested_urls.push_back(url);
        if (url.find("gemini-3.8-flash") != std::string::npos) {
            throw std::runtime_error("HTTP error 429: Resource exhausted / rate limit RESOURCE_EXHAUSTED");
        }
        return R"({"candidates":[{"content":{"parts":[{"text":"Fallback response successfully received"}]}}]})";
    };

    auto resp = adapter.sendMessage(
        "Hello",
        mock_post,
        "user",
        "gemini-3.8-flash"
    );

    EXPECT_FALSE(resp.choices.empty());
    EXPECT_EQ(resp.choices[0].message.content, "Fallback response successfully received");
    ASSERT_GE(requested_urls.size(), 2u);
    EXPECT_NE(requested_urls[0].find("gemini-3.8-flash"), std::string::npos);
    EXPECT_NE(requested_urls[1].find("gemini-3.1-flash-lite"), std::string::npos);
}

TEST(GeminiAdapterTest, FunctionCallingEmptyFinalTextFallback) {
    GeminiAdapter adapter("test_key");
    int turn = 0;
    auto mock_post = [&](const std::string&, const std::string&, auto) -> std::string {
        turn++;
        if (turn == 1) {
            return R"({
                "candidates": [{
                    "content": {
                        "parts": [{
                            "functionCall": {
                                "name": "select_theme",
                                "args": {"name": "Amber"}
                            }
                        }]
                    }
                }]
            })";
        } else {
            return R"({
                "candidates": [{
                    "content": {
                        "parts": []
                    }
                }]
            })";
        }
    };

    auto mock_executor = [](const std::string& name, const std::string&) -> std::string {
        EXPECT_EQ(name, "select_theme");
        return R"({"status":"success","message":"Theme 'Amber' selected successfully"})";
    };

    std::vector<std::string> schemas = {
        R"({"name":"select_theme","description":"Select theme","parameters":{"type":"object"}})"
    };

    auto resp = adapter.sendMessageWithFunctionCalling(
        "Change theme to Amber",
        mock_post,
        mock_executor,
        "user",
        "gemini-3.6-flash",
        "",
        0.5f,
        nullptr,
        &schemas
    );

    EXPECT_FALSE(resp.choices.empty());
    EXPECT_FALSE(resp.choices[0].message.content.empty());
    EXPECT_EQ(resp.choices[0].message.content, "Theme 'Amber' selected successfully");
    EXPECT_NE(resp.choices[0].message.content, "I have completed the requested operation.");
}

TEST(ProcessHelperTest, ExpandsTildeCorrectly) {
    const char* home = std::getenv("HOME");
    ASSERT_NE(home, nullptr);
    std::string expected = std::string(home) + "/src/rouen";
    EXPECT_EQ(ProcessHelper::expandTilde("~/src/rouen"), expected);
    EXPECT_EQ(ProcessHelper::expandTilde("~"), std::string(home));
    EXPECT_EQ(ProcessHelper::expandTilde("/var/log"), "/var/log");
    EXPECT_EQ(ProcessHelper::expandTilde(""), "");
}

TEST(GeminiAdapterTest, SerializesThoughtSignatureInFunctionCall) {
    GeminiAdapter adapter("dummy_api_key");
    std::vector<GeminiAdapter::Message> conversation;
    
    GeminiAdapter::Message msg;
    msg.role = "model";
    msg.function_calls.push_back({"run_local_command", "{\"command\":\"agy --version\"}", "call_123", "SIG_ABC_123"});
    conversation.push_back(msg);
    
    std::string request = adapter.build_gemini_request(conversation, 0.5f, false);
    
    glz::json_t doc;
    auto err = glz::read_json(doc, request);
    ASSERT_FALSE(err) << glz::format_error(err, request);
    
    auto contents = doc["contents"];
    ASSERT_EQ(contents.size(), 1u);
    auto parts = contents[0]["parts"];
    ASSERT_EQ(parts.size(), 1u);
    
    EXPECT_TRUE(parts[0].contains("thoughtSignature"));
    EXPECT_EQ(parts[0]["thoughtSignature"].get<std::string>(), "SIG_ABC_123");
}

TEST(GeminiAdapterTest, FallsBackToGemini3FlashPreviewOn429) {
    GeminiAdapter adapter("test_key");
    std::vector<std::string> requested_urls;

    auto mock_post = [&](const std::string& url, const std::string&, auto) -> std::string {
        requested_urls.push_back(url);
        if (url.find("gemini-3-flash-preview") == std::string::npos) {
            throw std::runtime_error("HTTP error 429: Resource exhausted");
        }
        return R"({"candidates":[{"content":{"parts":[{"text":"Fallback from gemini-3-flash-preview"}]}}]})";
    };

    auto resp = adapter.sendMessage(
        "Hello",
        mock_post,
        "user",
        "gemini-3.6-flash"
    );

    EXPECT_FALSE(resp.choices.empty());
    EXPECT_EQ(resp.choices[0].message.content, "Fallback from gemini-3-flash-preview");
    ASSERT_GE(requested_urls.size(), 2u);
    EXPECT_NE(requested_urls[0].find("gemini-3.6-flash"), std::string::npos);
    EXPECT_NE(requested_urls.back().find("gemini-3-flash-preview"), std::string::npos);
}

#include "../src/helpers/persona_manager.hpp"

TEST(PersonaManagerTest, ThinkingLevelConfigurationAndDefaults) {
    auto& pm = PersonaManager::instance();
    const auto& personas = pm.get_personas();
    ASSERT_FALSE(personas.empty());

    const Persona* dev_arch = nullptr;
    const Persona* default_p = nullptr;
    const Persona* term_p = nullptr;
    for (const auto& p : personas) {
        if (p.name == "Code & Git Architect") dev_arch = &p;
        else if (p.name == "Rouen Assistant") default_p = &p;
        else if (p.name == "Terminal Specialist") term_p = &p;
    }

    if (dev_arch) {
        EXPECT_EQ(dev_arch->get_effective_thinking_level(), "low");
    }
    if (default_p) {
        EXPECT_EQ(default_p->get_effective_thinking_level(), "high");
    }
    if (term_p) {
        EXPECT_EQ(term_p->get_effective_thinking_level(), "minimal");
    }

    // Verify mapping when thinking_level is empty
    Persona legacy_code;
    legacy_code.temperature = 0.1f;
    EXPECT_EQ(legacy_code.get_effective_thinking_level(), "minimal");

    Persona legacy_dev;
    legacy_dev.temperature = 0.2f;
    EXPECT_EQ(legacy_dev.get_effective_thinking_level(), "low");

    Persona legacy_chat;
    legacy_chat.temperature = 0.7f;
    EXPECT_EQ(legacy_chat.get_effective_thinking_level(), "high");
}

TEST(PersonaManagerTest, AutonomousEngineerPersonaRegistrationAndMCPs) {
    auto& pm = PersonaManager::instance();
    const auto& personas = pm.get_personas();
    
    const Persona* eng_p = nullptr;
    const Persona* assistant_p = nullptr;
    for (const auto& p : personas) {
        if (p.name == "Autonomous Engineer") {
            eng_p = &p;
        } else if (p.name == "Rouen Assistant") {
            assistant_p = &p;
        }
    }

    ASSERT_NE(eng_p, nullptr) << "Autonomous Engineer persona must be registered in PersonaManager";
    EXPECT_EQ(eng_p->get_effective_thinking_level(), "high");
    EXPECT_FLOAT_EQ(eng_p->temperature, 0.1f);
    EXPECT_TRUE(eng_p->allowed_personas.empty());

    // Verify allowed MCPs include editor, terminal, git, deck, adaptive_card
    std::vector<std::string> expected_mcps = {"editor", "terminal", "git", "deck", "adaptive_card"};
    for (const auto& mcp : expected_mcps) {
        EXPECT_NE(std::find(eng_p->allowed_mcps.begin(), eng_p->allowed_mcps.end(), mcp), eng_p->allowed_mcps.end())
            << "Autonomous Engineer must have allowed_mcp: " << mcp;
    }

    // Verify system prompt enforces TDD and -j2 parallelism
    EXPECT_NE(eng_p->system_prompt.find("-j2"), std::string::npos);
    EXPECT_NE(eng_p->system_prompt.find("code_read_file"), std::string::npos);
    EXPECT_NE(eng_p->system_prompt.find("code_apply_patch"), std::string::npos);
    EXPECT_NE(eng_p->system_prompt.find("run_local_command"), std::string::npos);

    // Verify Rouen Assistant delegates directly to Autonomous Engineer
    ASSERT_NE(assistant_p, nullptr);
    EXPECT_NE(std::find(assistant_p->allowed_personas.begin(), assistant_p->allowed_personas.end(), "Autonomous Engineer"), assistant_p->allowed_personas.end())
        << "Rouen Assistant must have Autonomous Engineer in allowed_personas";
}

TEST(PersonaManagerTest, AutonomousEngineerToolFilteringAndCategorization) {
    auto& pm = PersonaManager::instance();
    const Persona* eng_p = nullptr;
    for (const auto& p : pm.get_personas()) {
        if (p.name == "Autonomous Engineer") {
            eng_p = &p;
            break;
        }
    }
    ASSERT_NE(eng_p, nullptr);

    // Simulate AIChat tool categorization & MCP matching
    auto categorize = [](std::string_view func_name, std::string_view card_type) -> std::string {
        if (func_name == "run_local_command") return "terminal";
        if (func_name == "edit_file") return "editor";
        if (func_name.starts_with("code_")) {
            if (func_name == "code_generate_conventional_commit") return "git";
            return "editor";
        }
        return std::string(card_type);
    };

    auto allows_tool = [&](const Persona& persona, std::string_view func_name, std::string_view card_type) -> bool {
        std::string cat = categorize(func_name, card_type);
        if (cat == "git" || cat == "github") {
            return std::find(persona.allowed_mcps.begin(), persona.allowed_mcps.end(), "git") != persona.allowed_mcps.end() ||
                   std::find(persona.allowed_mcps.begin(), persona.allowed_mcps.end(), "github") != persona.allowed_mcps.end() ||
                   std::find(persona.allowed_mcps.begin(), persona.allowed_mcps.end(), "editor") != persona.allowed_mcps.end();
        }
        return std::find(persona.allowed_mcps.begin(), persona.allowed_mcps.end(), cat) != persona.allowed_mcps.end();
    };

    // Autonomous Engineer must allow all core tools simultaneously without multi-hop delegation
    EXPECT_TRUE(allows_tool(*eng_p, "run_local_command", "terminal"));
    EXPECT_TRUE(allows_tool(*eng_p, "code_read_file", "editor"));
    EXPECT_TRUE(allows_tool(*eng_p, "code_apply_patch", "editor"));
    EXPECT_TRUE(allows_tool(*eng_p, "code_write_file", "editor"));
    EXPECT_TRUE(allows_tool(*eng_p, "code_generate_conventional_commit", "editor"));
    EXPECT_TRUE(allows_tool(*eng_p, "create_card", "deck"));
}

TEST(GeminiAdapterTest, ConfigurableMaxOutputTokensInRequest) {
    GeminiAdapter adapter("dummy_api_key");
    std::vector<GeminiAdapter::Message> conversation;
    conversation.emplace_back("user", "Write a large C++ file");
    
    // Test with explicit max_output_tokens = 16384
    std::string request = adapter.build_gemini_request(conversation, 0.2f, false, "high", 16384);
    
    glz::json_t doc;
    auto err = glz::read_json(doc, request);
    ASSERT_FALSE(err) << glz::format_error(err, request);
    
    ASSERT_TRUE(doc.contains("generationConfig"));
    auto gen_config = doc["generationConfig"];
    EXPECT_TRUE(gen_config.contains("maxOutputTokens"));
    EXPECT_EQ(gen_config["maxOutputTokens"].get<double>(), 16384.0);
}

TEST(GeminiAdapterTest, SupportsConfigurableLoopDepthBeyondDefault) {
    GeminiAdapter adapter("dummy_api_key");
    int turn = 0;
    
    // Simulate 7 consecutive tool calls followed by final answer on turn 8
    auto mock_post = [&](const std::string&, const std::string&, auto) -> std::string {
        turn++;
        if (turn <= 7) {
            return std::format(R"({{
                "candidates": [{{
                    "content": {{
                        "parts": [{{
                            "functionCall": {{
                                "name": "step_tool",
                                "args": {{"step": {}}}
                            }}
                        }}]
                    }}
                }}]
            }})", turn);
        } else {
            return R"({
                "candidates": [{
                    "content": {
                        "parts": [{
                            "text": "Completed all 7 autonomous steps successfully."
                        }]
                    }
                }]
            })";
        }
    };

    int executed_steps = 0;
    auto mock_executor = [&](const std::string& name, const std::string&) -> std::string {
        EXPECT_EQ(name, "step_tool");
        executed_steps++;
        return std::format(R"({{"status":"success","step":{}}})", executed_steps);
    };

    std::vector<std::string> schemas = {
        R"({"name":"step_tool","description":"Perform step","parameters":{"type":"object"}})"
    };

    // Configure max_iterations = 10 (exceeding the old hardcoded 5)
    auto resp = adapter.sendMessageWithFunctionCalling(
        "Execute 7 steps",
        mock_post,
        mock_executor,
        "user",
        "gemini-3.8-flash",
        "",
        0.2f,
        nullptr,
        &schemas,
        "high",
        10,
        16384
    );

    EXPECT_EQ(executed_steps, 7);
    EXPECT_EQ(turn, 8);
    EXPECT_FALSE(resp.choices.empty());
    EXPECT_EQ(resp.choices[0].message.content, "Completed all 7 autonomous steps successfully.");
}

TEST(GeminiAdapterTest, SynthesizesResponseWhenMaxIterationsReached) {
    GeminiAdapter adapter("dummy_api_key");
    int turn = 0;
    bool received_synthesis_call = false;

    auto mock_post = [&](const std::string&, const std::string& body, auto) -> std::string {
        turn++;
        if (turn <= 2) {
            // Turns 1 & 2: model attempts tool calls
            return std::format(R"({{
                "candidates": [{{
                    "content": {{
                        "parts": [{{
                            "functionCall": {{
                                "name": "step_tool",
                                "args": {{"step": {}}}
                            }}
                        }}]
                    }}
                }}]
            }})", turn);
        } else {
            // Turn 3: Final synthesis turn! Must NOT have tools in the request
            glz::json_t req_doc;
            auto err = glz::read_json(req_doc, body);
            if (!err) {
                // Verified tools are omitted so model cannot call more tools
                EXPECT_FALSE(req_doc.contains("tools"));
            }
            received_synthesis_call = true;
            return R"({
                "candidates": [{
                    "content": {
                        "parts": [{
                            "text": "Autonomous report: steps 1 and 2 completed with verified status."
                        }]
                    }
                }]
            })";
        }
    };

    int executed_steps = 0;
    auto mock_executor = [&](const std::string&, const std::string&) -> std::string {
        executed_steps++;
        return std::format(R"({{"status":"success","step":{}}})", executed_steps);
    };

    std::vector<std::string> schemas = {
        R"({"name":"step_tool","description":"Perform step","parameters":{"type":"object"}})"
    };

    // Configure max_iterations = 2 to trigger turn ceiling
    auto resp = adapter.sendMessageWithFunctionCalling(
        "Execute 2 steps and stop",
        mock_post,
        mock_executor,
        "user",
        "gemini-3.8-flash",
        "",
        0.2f,
        nullptr,
        &schemas,
        "high",
        2,
        8192
    );

    EXPECT_EQ(executed_steps, 2);
    EXPECT_TRUE(received_synthesis_call);
    EXPECT_FALSE(resp.choices.empty());
    // Crucial: Must NOT return generic "I have completed the requested operation."
    EXPECT_NE(resp.choices[0].message.content, "I have completed the requested operation.");
    EXPECT_EQ(resp.choices[0].message.content, "Autonomous report: steps 1 and 2 completed with verified status.");
}

TEST(PersonaManagerTest, AutonomousEngineerExtendedIterationsAndTokens) {
    auto& pm = PersonaManager::instance();
    const Persona* eng_p = nullptr;
    const Persona* arch_p = nullptr;
    const Persona* def_p = nullptr;

    for (const auto& p : pm.get_personas()) {
        if (p.name == "Autonomous Engineer") eng_p = &p;
        else if (p.name == "Code & Git Architect") arch_p = &p;
        else if (p.name == "Rouen Assistant") def_p = &p;
    }

    ASSERT_NE(eng_p, nullptr);
    ASSERT_NE(arch_p, nullptr);
    ASSERT_NE(def_p, nullptr);

    EXPECT_EQ(eng_p->max_tool_iterations, 50);
    EXPECT_EQ(eng_p->max_output_tokens, 16384);

    EXPECT_EQ(arch_p->max_tool_iterations, 50);
    EXPECT_EQ(arch_p->max_output_tokens, 16384);

    EXPECT_EQ(def_p->max_tool_iterations, 25);
    EXPECT_EQ(def_p->max_output_tokens, 8192);
}

TEST(GeminiAdapterTest, DirectGlazeParsingWithEscapedQuotesAndCandidateFallback) {
    GeminiAdapter adapter("dummy_key");
    // Response with complex JSON, escaped characters, and extra metadata keys
    std::string response = R"({
        "candidates": [{
            "content": {
                "parts": [{
                    "text": "Found path: C:\\\\Users\\\\Alice\\\"Docs\\\" and file [report.txt]."
                }]
            },
            "finishReason": "STOP",
            "index": 0
        }],
        "usageMetadata": {"promptTokenCount": 12, "candidatesTokenCount": 34},
        "modelVersion": "gemini-3.8-flash"
    })";

    auto parsed = adapter.parse_gemini_response_full(response);
    ASSERT_FALSE(parsed.candidates.empty());
    ASSERT_FALSE(parsed.candidates[0].content.parts.empty());
    EXPECT_EQ(parsed.candidates[0].content.parts[0].text, "Found path: C:\\\\Users\\\\Alice\\\"Docs\\\" and file [report.txt].");
}

TEST(GeminiAdapterTest, SynthesisTurnIncludesOriginalUserQueryAnchor) {
    GeminiAdapter adapter("dummy_key");
    int turn = 0;
    std::string captured_synth_body;

    auto mock_post = [&](const std::string&, const std::string& body, auto) -> std::string {
        turn++;
        if (turn == 1) {
            return R"({
                "candidates": [{
                    "content": {
                        "parts": [{
                            "functionCall": {
                                "name": "check_status",
                                "args": {}
                            }
                        }]
                    }
                }]
            })";
        } else {
            captured_synth_body = body;
            return R"({
                "candidates": [{
                    "content": {
                        "parts": [{
                            "text": "Synthesized: Checked status and all systems are running."
                        }]
                    }
                }]
            })";
        }
    };

    auto mock_executor = [](const std::string&, const std::string&) -> std::string {
        return R"({"status":"ok"})";
    };

    std::vector<std::string> schemas = {
        R"({"name":"check_status","description":"Check status","parameters":{"type":"object"}})"
    };

    auto resp = adapter.sendMessageWithFunctionCalling(
        "Please check the system health status",
        mock_post,
        mock_executor,
        "user",
        "gemini-3.8-flash",
        "",
        0.2f,
        nullptr,
        &schemas,
        "high",
        1,
        4096
    );

    EXPECT_FALSE(captured_synth_body.empty());
    EXPECT_NE(captured_synth_body.find("Please check the system health status"), std::string::npos);
    EXPECT_NE(resp.choices[0].message.content, "I have completed the requested operation.");
    EXPECT_EQ(resp.choices[0].message.content, "Synthesized: Checked status and all systems are running.");
}
