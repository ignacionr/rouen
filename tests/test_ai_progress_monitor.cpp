#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <gtest/gtest.h>

#include "../src/helpers/ai_progress_monitor.hpp"
#include "../src/helpers/persona_manager.hpp"
#include "../src/hosts/llm_host.hpp"

namespace {

using namespace rouen::helpers;

TEST(AIProgressMonitorTest, CheckpointScheduleCalculation) {
    // 0 -> 30 -> 60 -> 120 -> 300 -> 600 -> 900 -> 1200
    EXPECT_EQ(next_progress_checkpoint_seconds(0), 30);
    EXPECT_EQ(next_progress_checkpoint_seconds(30), 60);
    EXPECT_EQ(next_progress_checkpoint_seconds(60), 120);
    EXPECT_EQ(next_progress_checkpoint_seconds(120), 300);
    EXPECT_EQ(next_progress_checkpoint_seconds(300), 600);
    EXPECT_EQ(next_progress_checkpoint_seconds(600), 900);
    EXPECT_EQ(next_progress_checkpoint_seconds(900), 1200);

    // Intermediate values
    EXPECT_EQ(next_progress_checkpoint_seconds(-5), 30);
    EXPECT_EQ(next_progress_checkpoint_seconds(15), 30);
    EXPECT_EQ(next_progress_checkpoint_seconds(45), 60);
    EXPECT_EQ(next_progress_checkpoint_seconds(95), 120);
    EXPECT_EQ(next_progress_checkpoint_seconds(250), 300);
    EXPECT_EQ(next_progress_checkpoint_seconds(450), 600);
}

TEST(AIProgressMonitorTest, StructuredAssessmentParsingAndFallback) {
    ExecutionTelemetrySnapshot snapshot;
    snapshot.channel_name = "ai_chat";
    snapshot.active_persona_name = "Autonomous Engineer";
    snapshot.active_model_name = "gemini-3.8-flash";
    snapshot.user_request = "Run unit tests and deploy Rouen";
    snapshot.elapsed_seconds = 60;
    snapshot.checkpoint_seconds = 60;
    snapshot.completed_turns = 3;
    snapshot.current_step = "run_unit_tests";
    snapshot.recent_tools = {
        ToolTelemetryEntry{.tool_name = "code_read_file", .summary = "src/main.cpp", .is_running = false, .duration_ms = 120},
        ToolTelemetryEntry{.tool_name = "run_unit_tests", .summary = "test_mcp", .is_running = true, .duration_ms = 0}
    };

    // 1. Valid raw JSON
    const std::string valid_json = R"({
        "summary": "Read main.cpp and currently executing unit test suite test_mcp.",
        "current_activity": "Running test_mcp",
        "estimated_remaining_seconds": 45,
        "confidence": "high"
    })";
    auto parsed = parse_progress_assessment_response(valid_json, snapshot);
    EXPECT_TRUE(parsed.from_llm);
    EXPECT_EQ(parsed.summary, "Read main.cpp and currently executing unit test suite test_mcp.");
    EXPECT_EQ(parsed.current_activity, "Running test_mcp");
    EXPECT_EQ(parsed.estimated_remaining_seconds, 45);
    EXPECT_EQ(parsed.confidence, "high");
    EXPECT_EQ(parsed.elapsed_seconds, 60);
    EXPECT_EQ(parsed.completed_turns, 3u);

    // 2. Markdown-fenced JSON with current_step alias
    const std::string fenced_json =
        "Here is the assessment:\n```json\n"
        "{\n"
        "  \"summary\": \"Compiling test binary and verifying assertions.\",\n"
        "  \"current_step\": \"Building test_mcp\",\n"
        "  \"estimated_remaining_seconds\": 90,\n"
        "  \"confidence\": \"medium\"\n"
        "}\n```";
    auto parsed_fenced = parse_progress_assessment_response(fenced_json, snapshot);
    EXPECT_TRUE(parsed_fenced.from_llm);
    EXPECT_EQ(parsed_fenced.summary, "Compiling test binary and verifying assertions.");
    EXPECT_EQ(parsed_fenced.current_activity, "Building test_mcp");
    EXPECT_EQ(parsed_fenced.estimated_remaining_seconds, 90);
    EXPECT_EQ(parsed_fenced.confidence, "medium");

    // 3. Fallback when JSON is malformed or LLM is unavailable
    auto fallback = parse_progress_assessment_response("not valid json {{{", snapshot);
    EXPECT_FALSE(fallback.from_llm);
    EXPECT_FALSE(fallback.summary.empty());
    EXPECT_NE(fallback.summary.find("run_unit_tests"), std::string::npos);
    EXPECT_EQ(fallback.current_activity, "run_unit_tests");
    EXPECT_EQ(fallback.elapsed_seconds, 60);
    EXPECT_EQ(fallback.completed_turns, 3u);
    EXPECT_GE(fallback.estimated_remaining_seconds, 15);

    // 4. Channel formatting check
    std::string formatted = format_progress_message_for_channel(parsed, "telegram");
    EXPECT_NE(formatted.find("1m 00s elapsed"), std::string::npos);
    EXPECT_NE(formatted.find("~45s remaining"), std::string::npos);
    EXPECT_NE(formatted.find("Read main.cpp"), std::string::npos);
}

TEST(AIProgressMonitorTest, ProgressMonitorPersonaManagement) {
    auto& pm = PersonaManager::instance();
    const std::string original_monitor = pm.get_progress_monitor_persona();

    // Built-in "Progress Monitor" must be seeded
    const Persona* monitor_p = pm.get_persona_by_name("Progress Monitor");
    ASSERT_NE(monitor_p, nullptr);
    EXPECT_FLOAT_EQ(monitor_p->temperature, 0.1f);
    EXPECT_EQ(monitor_p->get_effective_thinking_level(), "minimal");
    EXPECT_EQ(monitor_p->max_tool_iterations, 0);
    EXPECT_EQ(monitor_p->max_output_tokens, 512);
    EXPECT_TRUE(monitor_p->allowed_mcps.empty());
    EXPECT_FALSE(monitor_p->system_prompt.empty());
    EXPECT_NE(monitor_p->system_prompt.find("estimated_remaining_seconds"), std::string::npos);

    // Fast LLM config resolution
    auto fast_cfg = rouen::hosts::LLMHost::get_progress_monitor_config(monitor_p->llm_config_name);
    EXPECT_FALSE(fast_cfg.model_name.empty());

    // Selection & directory export/import persistence
    pm.set_progress_monitor_persona("Rouen Assistant");
    EXPECT_EQ(pm.get_progress_monitor_persona(), "Rouen Assistant");

    auto tmp_dir = std::filesystem::temp_directory_path() / "rouen_test_progress_monitor_personas";
    std::filesystem::remove_all(tmp_dir);
    ASSERT_TRUE(pm.export_to_directory(tmp_dir));

    pm.set_progress_monitor_persona("Progress Monitor");
    EXPECT_EQ(pm.get_progress_monitor_persona(), "Progress Monitor");

    ASSERT_TRUE(pm.import_from_directory(tmp_dir));
    EXPECT_EQ(pm.get_progress_monitor_persona(), "Rouen Assistant");

    // Restore original setting and clean up
    pm.set_progress_monitor_persona(original_monitor.empty() ? "Progress Monitor" : original_monitor);
    std::filesystem::remove_all(tmp_dir);
}

TEST(AIProgressMonitorTest, TimerTriggerAndEarlyCancellation) {
    std::mutex cb_mutex;
    std::vector<ProgressAssessment> assessments;

    AIProgressMonitor::Options opts;
    opts.channel_name = "telegram";
    opts.active_persona_name = "Rouen Assistant";
    opts.active_model_name = "gemini-3.8-flash";
    opts.user_request = "Analyze system logs and summarize";
    opts.second_duration = std::chrono::milliseconds(1); // 1ms per simulated second -> 30s = 30ms, 60s = 60ms
    opts.custom_evaluator = [](const std::string& /*sys*/, const std::string& /*usr*/, const std::string& /*persona*/) {
        return R"({"summary":"Analyzing logs in progress","current_activity":"log_scan","estimated_remaining_seconds":25,"confidence":"high"})";
    };
    opts.on_progress = [&](const ProgressAssessment& a) {
        std::lock_guard<std::mutex> lock(cb_mutex);
        assessments.push_back(a);
    };

    AIProgressMonitor monitor(std::move(opts));
    monitor.record_tool_start("run_local_command", "tail -n 100 /var/log/system.log");
    monitor.start();

    // Wait long enough for 30s (30ms) and 60s (60ms) checkpoints to fire
    for (int i = 0; i < 60; ++i) {
        {
            std::lock_guard<std::mutex> lock(cb_mutex);
            if (assessments.size() >= 2) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Complete immediately and ensure no further callbacks fire after stop
    monitor.record_tool_end("run_local_command", 42);
    monitor.record_turn_completed(1);
    monitor.mark_completed();

    size_t count_at_stop = 0;
    {
        std::lock_guard<std::mutex> lock(cb_mutex);
        count_at_stop = assessments.size();
        ASSERT_GE(count_at_stop, 2u);
        EXPECT_EQ(assessments[0].checkpoint_seconds, 30);
        EXPECT_EQ(assessments[1].checkpoint_seconds, 60);
        EXPECT_EQ(assessments[0].estimated_remaining_seconds, 25);
        EXPECT_EQ(assessments[0].summary, "Analyzing logs in progress");
    }

    // Sleep another 80ms (would have crossed 120s checkpoint if still running)
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    {
        std::lock_guard<std::mutex> lock(cb_mutex);
        EXPECT_EQ(assessments.size(), count_at_stop);
    }
}

TEST(AIProgressMonitorTest, GuaranteedTimeoutAndErrorRetransmission) {
    // 1. Timeout error classification and retransmission
    ErrorTransmissionReport captured_timeout;
    bool timeout_called = false;

    AIProgressMonitor::Options opts;
    opts.channel_name = "telegram";
    opts.active_persona_name = "Autonomous Engineer";
    opts.active_model_name = "gemini-4-argon";
    opts.user_request = "Build full project";
    opts.on_error_or_timeout = [&](const ErrorTransmissionReport& rep) {
        captured_timeout = rep;
        timeout_called = true;
    };

    AIProgressMonitor monitor(std::move(opts));
    monitor.record_tool_start("build_and_deploy", "cmake --build build -j2");
    monitor.record_turn_completed(2);

    auto report = monitor.notify_error_or_timeout("HTTP request failed: Operation timed out after 180000 milliseconds");
    EXPECT_TRUE(timeout_called);
    EXPECT_TRUE(report.is_timeout);
    EXPECT_EQ(report.kind, ExecutionFailureKind::Timeout);
    EXPECT_EQ(report.completed_turns, 2u);
    EXPECT_NE(report.formatted_channel_message.find("Timeout"), std::string::npos);
    EXPECT_NE(report.formatted_channel_message.find("Autonomous Engineer"), std::string::npos);
    EXPECT_NE(report.formatted_channel_message.find("build_and_deploy"), std::string::npos);

    // Calling notify_error_or_timeout a second time should be idempotent (no duplicate transmission)
    timeout_called = false;
    monitor.notify_error_or_timeout("Duplicate error");
    EXPECT_FALSE(timeout_called);

    // 2. General exception / provider error retransmission via finalize_if_unreported
    ErrorTransmissionReport captured_err;
    bool err_called = false;
    AIProgressMonitor::Options opts2;
    opts2.channel_name = "ai_chat";
    opts2.active_persona_name = "Rouen Assistant";
    opts2.active_model_name = "gemini-3.8-flash";
    opts2.on_error_or_timeout = [&](const ErrorTransmissionReport& rep) {
        captured_err = rep;
        err_called = true;
    };

    AIProgressMonitor monitor2(std::move(opts2));
    monitor2.set_current_step("calling sub-persona Code & Git Architect");
    monitor2.finalize_if_unreported("Provider returned HTTP 502 Bad Gateway");
    EXPECT_TRUE(err_called);
    EXPECT_FALSE(captured_err.is_timeout);
    EXPECT_NE(captured_err.formatted_channel_message.find("502 Bad Gateway"), std::string::npos);
    EXPECT_NE(captured_err.formatted_channel_message.find("Rouen Assistant"), std::string::npos);
}

} // namespace
