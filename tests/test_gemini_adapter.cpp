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

TEST(GeminiAdapterTest, SerializesFunctionCallCorrectly) {
    GeminiAdapter adapter("dummy_api_key");
    std::vector<GeminiAdapter::Message> conversation;
    
    GeminiAdapter::Message msg;
    msg.role = "model";
    msg.content = "I need to run a command.";
    msg.function_calls.push_back({"run_local_command", "{\"command\":\"git status\"}"});
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


