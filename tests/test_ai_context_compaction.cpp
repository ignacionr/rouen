#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include "../src/helpers/persona_manager.hpp"
#include "../src/helpers/context_compactor.hpp"

namespace test_helpers {
    inline void assert_true(bool condition, const std::string& message) {
        if (!condition) {
            std::cerr << "❌ Assertion failed: " << message << "\n";
            std::exit(1);
        } else {
            std::cout << "✅ " << message << ": PASSED\n";
        }
    }
}

void test_persona_compaction_fields() {
    std::cout << "\n--- Testing Persona Compaction Settings ---\n";
    rouen::helpers::Persona persona;
    persona.name = "Test Assistant";
    persona.max_context_turns = 20;
    persona.preserve_recent_turns = 6;
    persona.context_compaction_strategy = "summarize";

    // Test Glaze JSON serialization and deserialization
    std::string json_str = glz::write<glz::opts{.prettify = true}>(persona).value_or("");
    test_helpers::assert_true(!json_str.empty(), "Persona serializes to JSON");
    test_helpers::assert_true(json_str.find("max_context_turns") != std::string::npos, "JSON contains max_context_turns");
    test_helpers::assert_true(json_str.find("preserve_recent_turns") != std::string::npos, "JSON contains preserve_recent_turns");
    test_helpers::assert_true(json_str.find("context_compaction_strategy") != std::string::npos, "JSON contains context_compaction_strategy");

    rouen::helpers::Persona deserialized;
    auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(deserialized, json_str);
    test_helpers::assert_true(!err, "Persona deserializes without error");
    test_helpers::assert_true(deserialized.max_context_turns == 20, "max_context_turns roundtripped accurately");
    test_helpers::assert_true(deserialized.preserve_recent_turns == 6, "preserve_recent_turns roundtripped accurately");
    test_helpers::assert_true(deserialized.context_compaction_strategy == "summarize", "context_compaction_strategy roundtripped accurately");
}

void test_context_not_compacted_below_threshold() {
    std::cout << "\n--- Testing Context Not Compacted Below Threshold ---\n";
    rouen::helpers::Persona persona;
    persona.max_context_turns = 10;
    persona.preserve_recent_turns = 4;
    persona.context_compaction_strategy = "summarize";

    std::vector<std::pair<std::string, std::string>> conversation;
    for (int i = 1; i <= 8; ++i) {
        if (i % 2 == 1) {
            conversation.emplace_back("user", "User message " + std::to_string(i));
        } else {
            conversation.emplace_back("assistant", "Assistant response " + std::to_string(i));
        }
    }

    auto result = rouen::helpers::ContextCompactor::compact(conversation, persona);
    test_helpers::assert_true(!result.was_compacted, "was_compacted is false when below threshold");
    test_helpers::assert_true(result.compacted_turns == 0, "compacted_turns is 0 when below threshold");
    test_helpers::assert_true(result.compacted_conversation.size() == 8, "Compacted conversation preserves all 8 messages");
    test_helpers::assert_true(result.compacted_conversation[0].second == "User message 1", "First message remains unchanged");
    test_helpers::assert_true(result.compacted_conversation[7].second == "Assistant response 8", "Last message remains unchanged");
}

void test_compaction_when_exceeded_with_verbatim_recent_turns() {
    std::cout << "\n--- Testing Compaction Threshold Exceeded & Verbatim Recent Preservation ---\n";
    rouen::helpers::Persona persona;
    persona.max_context_turns = 10;
    persona.preserve_recent_turns = 4;
    persona.context_compaction_strategy = "summarize";

    std::vector<std::pair<std::string, std::string>> conversation;
    for (int i = 1; i <= 14; ++i) {
        if (i % 2 == 1) {
            conversation.emplace_back("user", "User turn " + std::to_string(i));
        } else {
            conversation.emplace_back("assistant", "Assistant turn " + std::to_string(i));
        }
    }

    // 14 turns > 10 (max_context_turns).
    // preserve_recent_turns = 4.
    // Older turns = 14 - 4 = 10 turns.
    auto result = rouen::helpers::ContextCompactor::compact(
        conversation, 
        persona, 
        "",
        [](const std::string&, const std::string&) -> std::string {
            return "- User discussed turns 1 to 10.\n- Technical decisions were made.";
        }
    );

    test_helpers::assert_true(result.was_compacted, "was_compacted is true when history exceeds max_context_turns");
    test_helpers::assert_true(result.compacted_turns == 10, "compacted_turns is 10");
    // Result should have: 1 summary block message + 4 recent turns = 5 total messages
    test_helpers::assert_true(result.compacted_conversation.size() == 5, "Compacted conversation size is 1 summary + 4 recent turns");

    // Check summary block prefix
    test_helpers::assert_true(result.compacted_conversation[0].first == "system", "Summary block has system role");
    test_helpers::assert_true(result.compacted_conversation[0].second.starts_with(std::string(rouen::helpers::ContextCompactor::kSummaryHeaderPrefix)), "Summary block begins with expected header prefix");
    test_helpers::assert_true(result.compacted_conversation[0].second.find("User discussed turns 1 to 10.") != std::string::npos, "Summary block contains generated summary");

    // Verify recent turns are preserved verbatim
    test_helpers::assert_true(result.compacted_conversation[1].first == "user" && result.compacted_conversation[1].second == "User turn 11", "Recent turn 1 preserved verbatim");
    test_helpers::assert_true(result.compacted_conversation[2].first == "assistant" && result.compacted_conversation[2].second == "Assistant turn 12", "Recent turn 2 preserved verbatim");
    test_helpers::assert_true(result.compacted_conversation[3].first == "user" && result.compacted_conversation[3].second == "User turn 13", "Recent turn 3 preserved verbatim");
    test_helpers::assert_true(result.compacted_conversation[4].first == "assistant" && result.compacted_conversation[4].second == "Assistant turn 14", "Recent turn 4 preserved verbatim");
}

void test_compaction_fallback_summarizer() {
    std::cout << "\n--- Testing Fallback Structured Summarizer (No LLM) ---\n";
    rouen::helpers::Persona persona;
    persona.max_context_turns = 6;
    persona.preserve_recent_turns = 2;
    persona.context_compaction_strategy = "summarize";

    std::vector<std::pair<std::string, std::string>> conversation = {
        {"user", "Please refactor the mesh networking in src/hosts/rouen_mesh_host.cpp"},
        {"assistant", "I have reviewed src/hosts/rouen_mesh_host.cpp and identified the pairing handshake loop."},
        {"user", "We decided to use Ed25519 challenge response for pairing authentication."},
        {"assistant", "Understood. Applying the Ed25519 authentication mechanism now."},
        {"user", "Let's also check the unit tests in tests/test_rouen_mesh.cpp"},
        {"assistant", "Tests in tests/test_rouen_mesh.cpp passed cleanly with Ninja -j2."},
        {"user", "Now what is the current mesh status?"},
        {"assistant", "The mesh host is active and listening on port 8081."}
    };

    // 8 messages > 6. preserve_recent_turns = 2. Older = 6 messages.
    // Call without custom summarizer -> triggers generate_fallback_summary.
    auto result = rouen::helpers::ContextCompactor::compact(conversation, persona);
    test_helpers::assert_true(result.was_compacted, "Fallback summarization triggers successfully");
    test_helpers::assert_true(result.compacted_turns == 6, "Compacted 6 older turns");
    test_helpers::assert_true(!result.summary.empty(), "Generated fallback summary is non-empty");
    test_helpers::assert_true(result.summary.find("mesh") != std::string::npos, "Summary captured topic 'mesh'");
    test_helpers::assert_true(result.summary.find("rouen_mesh_host.cpp") != std::string::npos, "Summary preserved file path 'rouen_mesh_host.cpp'");
    test_helpers::assert_true(result.compacted_conversation.size() == 3, "Output has 1 summary + 2 recent turns");
    test_helpers::assert_true(result.compacted_conversation[1].second == "Now what is the current mesh status?", "Recent turn preserved");
    test_helpers::assert_true(result.compacted_conversation[2].second == "The mesh host is active and listening on port 8081.", "Recent turn preserved");
}

void test_compaction_strategy_none_and_sliding_window() {
    std::cout << "\n--- Testing 'none' and 'sliding_window' Strategies ---\n";
    rouen::helpers::Persona persona;
    persona.max_context_turns = 4;
    persona.preserve_recent_turns = 2;

    std::vector<std::pair<std::string, std::string>> conversation = {
        {"user", "Turn 1"},
        {"assistant", "Turn 2"},
        {"user", "Turn 3"},
        {"assistant", "Turn 4"},
        {"user", "Turn 5"},
        {"assistant", "Turn 6"}
    };

    // Test 'none':
    persona.context_compaction_strategy = "none";
    auto res_none = rouen::helpers::ContextCompactor::compact(conversation, persona);
    test_helpers::assert_true(!res_none.was_compacted, "strategy 'none' does not compact");
    test_helpers::assert_true(res_none.compacted_conversation.size() == 6, "strategy 'none' retains all 6 turns");

    // Test 'sliding_window':
    persona.context_compaction_strategy = "sliding_window";
    auto res_sw = rouen::helpers::ContextCompactor::compact(conversation, persona);
    test_helpers::assert_true(res_sw.was_compacted, "strategy 'sliding_window' marks compacted");
    test_helpers::assert_true(res_sw.compacted_turns == 4, "strategy 'sliding_window' compacts 4 turns");
    test_helpers::assert_true(res_sw.compacted_conversation.size() == 2, "strategy 'sliding_window' keeps only 2 recent turns");
    test_helpers::assert_true(res_sw.compacted_conversation[0].second == "Turn 5", "First preserved turn is Turn 5");
    test_helpers::assert_true(res_sw.compacted_conversation[1].second == "Turn 6", "Second preserved turn is Turn 6");
}

void test_clear_resets_history_and_context_summary() {
    std::cout << "\n--- Testing Clear Chat Resets History & Context Summary ---\n";
    rouen::helpers::Persona persona;
    persona.max_context_turns = 4;
    persona.preserve_recent_turns = 2;
    persona.context_compaction_strategy = "summarize";

    std::vector<std::pair<std::string, std::string>> conversation = {
        {"user", "Initial turn 1"},
        {"assistant", "Initial turn 2"},
        {"user", "Initial turn 3"},
        {"assistant", "Initial turn 4"},
        {"user", "Initial turn 5"}
    };

    std::string context_summary;
    size_t compacted_turns = 0;

    auto res = rouen::helpers::ContextCompactor::compact(conversation, persona, context_summary);
    test_helpers::assert_true(res.was_compacted, "Initial conversation compacted");
    context_summary = res.summary;
    compacted_turns = res.compacted_turns;
    test_helpers::assert_true(!context_summary.empty(), "Context summary generated");
    test_helpers::assert_true(compacted_turns == 3, "3 turns compacted");

    // Simulate Clear Chat action: both history and context_summary are cleared
    conversation.clear();
    context_summary.clear();
    compacted_turns = 0;

    test_helpers::assert_true(conversation.empty(), "Conversation cleared");
    test_helpers::assert_true(context_summary.empty(), "Context summary reset to empty");
    test_helpers::assert_true(compacted_turns == 0, "Compacted turns reset to 0");

    // Add fresh message after clear
    conversation.emplace_back("user", "Fresh start after clear");
    auto res2 = rouen::helpers::ContextCompactor::compact(conversation, persona, context_summary);
    test_helpers::assert_true(!res2.was_compacted, "Fresh post-clear conversation not compacted");
    test_helpers::assert_true(res2.compacted_conversation.size() == 1, "Fresh post-clear conversation has 1 message");
    test_helpers::assert_true(res2.compacted_conversation[0].second == "Fresh start after clear", "Fresh message retained verbatim without stale summary");
}

int main() {
    test_persona_compaction_fields();
    test_context_not_compacted_below_threshold();
    test_compaction_when_exceeded_with_verbatim_recent_turns();
    test_compaction_fallback_summarizer();
    test_compaction_strategy_none_and_sliding_window();
    test_clear_resets_history_and_context_summary();
    std::cout << "\nAll AI Context Compaction tests passed successfully!\n";
    return 0;
}
