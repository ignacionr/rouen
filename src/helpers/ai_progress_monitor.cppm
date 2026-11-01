module;

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module rouen.helpers.ai_progress_monitor;

export namespace rouen::helpers {

struct ProgressAssessment {
    std::string summary{};
    std::string current_activity{};
    int64_t estimated_remaining_seconds{-1};
    std::string confidence{"medium"};
    int64_t elapsed_seconds{0};
    uint32_t completed_turns{0};
    int64_t checkpoint_seconds{0};
    std::string monitor_persona_name{"Progress Monitor"};
    bool from_llm{false};
};

struct ToolTelemetryEntry {
    std::string tool_name{};
    std::string summary{};
    bool is_running{false};
    int64_t duration_ms{0};
};

struct ExecutionTelemetrySnapshot {
    std::string channel_name{};
    std::string active_persona_name{};
    std::string active_model_name{};
    std::string user_request{};
    int64_t elapsed_seconds{0};
    int64_t checkpoint_seconds{0};
    uint32_t completed_turns{0};
    std::string current_step{};
    std::vector<ToolTelemetryEntry> recent_tools{};
    std::vector<std::pair<std::string, std::string>> recent_conversation{};
};

enum class ExecutionFailureKind {
    None,
    Timeout,
    Cancelled,
    ProviderError,
    EmptyResponse,
    UnhandledException
};

struct ErrorTransmissionReport {
    ExecutionFailureKind kind{ExecutionFailureKind::UnhandledException};
    bool is_timeout{false};
    bool is_cancelled{false};
    int64_t elapsed_seconds{0};
    uint32_t completed_turns{0};
    std::string active_persona_name{};
    std::string active_model_name{};
    std::string last_activity{};
    std::string raw_error{};
    std::string formatted_channel_message{};
};

// Returns the next checkpoint in seconds after `last_checkpoint_seconds`:
// 0 -> 30 -> 60 -> 120 -> 300 -> 600 -> 900 -> ...
constexpr int64_t next_progress_checkpoint_seconds(int64_t last_checkpoint_seconds) noexcept {
    if (last_checkpoint_seconds < 30) return 30;
    if (last_checkpoint_seconds < 60) return 60;
    if (last_checkpoint_seconds < 120) return 120;
    if (last_checkpoint_seconds < 300) return 300;
    return ((last_checkpoint_seconds / 300) + 1) * 300;
}

[[nodiscard]] std::string format_duration_compact(int64_t seconds);
[[nodiscard]] std::string format_remaining_duration(int64_t estimated_remaining_seconds);
[[nodiscard]] std::string format_progress_message_for_channel(
    const ProgressAssessment& assessment,
    std::string_view channel = "generic"
);

[[nodiscard]] ProgressAssessment build_fallback_progress_assessment(
    const ExecutionTelemetrySnapshot& snapshot
);

[[nodiscard]] ProgressAssessment parse_progress_assessment_response(
    std::string_view raw_llm_output,
    const ExecutionTelemetrySnapshot& snapshot
);

[[nodiscard]] bool is_timeout_error_message(std::string_view error_text) noexcept;
[[nodiscard]] bool is_cancellation_error_message(std::string_view error_text) noexcept;

[[nodiscard]] ErrorTransmissionReport build_error_transmission_report(
    std::string_view raw_error,
    const ExecutionTelemetrySnapshot& snapshot,
    ExecutionFailureKind explicit_kind = ExecutionFailureKind::None
);

class AIProgressMonitor {
public:
    using AssessmentEvaluator = std::function<std::string(
        const std::string& system_prompt,
        const std::string& user_prompt,
        const std::string& monitor_persona_name
    )>;
    using ProgressCallback = std::function<void(const ProgressAssessment&)>;
    using ErrorCallback = std::function<void(const ErrorTransmissionReport&)>;

    struct Options {
        std::string channel_name{"generic"};
        std::string active_persona_name{};
        std::string active_model_name{};
        std::string user_request{};
        std::string monitor_persona_name{};
        std::vector<std::pair<std::string, std::string>> recent_conversation{};
        std::chrono::milliseconds second_duration{1000};
        AssessmentEvaluator custom_evaluator{};
        ProgressCallback on_progress{};
        ErrorCallback on_error_or_timeout{};
    };

    explicit AIProgressMonitor(Options options);
    ~AIProgressMonitor();

    AIProgressMonitor(const AIProgressMonitor&) = delete;
    AIProgressMonitor& operator=(const AIProgressMonitor&) = delete;

    void start();
    void stop() noexcept;

    void record_tool_start(std::string_view tool_name, std::string_view summary = "");
    void record_tool_end(std::string_view tool_name, int64_t duration_ms = 0);
    void record_turn_completed(uint32_t total_turns = 0);
    void set_current_step(std::string_view step);
    void set_active_model(std::string_view model_name);

    void mark_completed() noexcept;

    ErrorTransmissionReport notify_error_or_timeout(
        std::string_view error_message,
        ExecutionFailureKind explicit_kind = ExecutionFailureKind::None
    );

    ErrorTransmissionReport notify_cancelled(
        std::string_view reason = "Execution cancelled by user."
    );

    void finalize_if_unreported(std::string_view fallback_error);

    [[nodiscard]] ExecutionTelemetrySnapshot get_telemetry_snapshot() const;
    [[nodiscard]] std::optional<ProgressAssessment> get_latest_assessment() const;
    [[nodiscard]] std::vector<ProgressAssessment> get_assessment_history() const;

    static void set_default_evaluator(AssessmentEvaluator evaluator);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rouen::helpers
