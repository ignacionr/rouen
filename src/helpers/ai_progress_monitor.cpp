module;

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "glaze_include.hpp"
#include "persona_manager.hpp"
#include "../registrar.hpp"

module rouen.helpers.ai_progress_monitor;

namespace {

struct ProgressAssessmentWire {
    std::string summary{};
    std::string current_activity{};
    std::string current_step{};
    int64_t estimated_remaining_seconds{-1};
    std::string confidence{"medium"};
};

std::string to_lower_ascii(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (char const c : input) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string extract_json_object_slice(std::string_view raw) {
    size_t const first_brace = raw.find('{');
    if (first_brace == std::string_view::npos) {
        return {};
    }
    size_t const last_brace = raw.rfind('}');
    if (last_brace == std::string_view::npos || last_brace <= first_brace) {
        return {};
    }
    return std::string(raw.substr(first_brace, last_brace - first_brace + 1));
}

std::mutex& default_eval_mutex() {
    static std::mutex m;
    return m;
}

rouen::helpers::AIProgressMonitor::AssessmentEvaluator& default_eval_storage() {
    static rouen::helpers::AIProgressMonitor::AssessmentEvaluator eval;
    return eval;
}

std::string build_monitor_user_prompt(const rouen::helpers::ExecutionTelemetrySnapshot& snap) {
    std::ostringstream oss;
    oss << "In-Flight AI Execution Telemetry:\n";
    oss << "- Channel: " << (snap.channel_name.empty() ? "generic" : snap.channel_name) << "\n";
    oss << "- Active Persona: " << (snap.active_persona_name.empty() ? "Unknown" : snap.active_persona_name) << "\n";
    if (!snap.active_model_name.empty()) {
        oss << "- Primary Model: " << snap.active_model_name << "\n";
    }
    oss << "- Elapsed Time: " << snap.elapsed_seconds << " seconds (checkpoint: " << snap.checkpoint_seconds << "s)\n";
    oss << "- Completed Internal Turns: " << snap.completed_turns << "\n";
    if (!snap.current_step.empty()) {
        oss << "- Current Step / Activity: " << snap.current_step << "\n";
    }
    if (!snap.user_request.empty()) {
        std::string req_preview = snap.user_request;
        if (req_preview.size() > 400) {
            req_preview = req_preview.substr(0, 397) + "...";
        }
        oss << "- User Request: " << req_preview << "\n";
    }

    if (!snap.recent_tools.empty()) {
        oss << "- Tool & Persona Call Trace (" << snap.recent_tools.size() << " total):\n";
        size_t const start_idx = snap.recent_tools.size() > 8 ? snap.recent_tools.size() - 8 : 0;
        for (size_t i = start_idx; i < snap.recent_tools.size(); ++i) {
            const auto& t = snap.recent_tools[i];
            oss << "  * [" << (t.is_running ? "RUNNING" : "DONE") << "] " << t.tool_name;
            if (!t.summary.empty()) {
                oss << " (" << t.summary << ")";
            }
            if (!t.is_running && t.duration_ms > 0) {
                oss << " - " << t.duration_ms << "ms";
            }
            oss << "\n";
        }
    }

    if (!snap.recent_conversation.empty()) {
        oss << "- Recent Conversation Context:\n";
        size_t const start_idx = snap.recent_conversation.size() > 4 ? snap.recent_conversation.size() - 4 : 0;
        for (size_t i = start_idx; i < snap.recent_conversation.size(); ++i) {
            std::string snippet = snap.recent_conversation[i].second;
            if (snippet.size() > 160) {
                snippet = snippet.substr(0, 157) + "...";
            }
            oss << "  [" << snap.recent_conversation[i].first << "]: " << snippet << "\n";
        }
    }

    oss << "\nProduce the JSON progress assessment now.";
    return oss.str();
}

} // namespace

template <>
struct glz::meta<ProgressAssessmentWire> {
    using T = ProgressAssessmentWire;
    static constexpr auto value = object(
        "summary", &T::summary,
        "current_activity", &T::current_activity,
        "current_step", &T::current_step,
        "estimated_remaining_seconds", &T::estimated_remaining_seconds,
        "confidence", &T::confidence
    );
};

namespace rouen::helpers {

std::string format_duration_compact(int64_t seconds) {
    int64_t const clamped = std::max<int64_t>(0, seconds);
    if (clamped < 60) {
        return std::format("{}s", clamped);
    }
    int64_t const mins = clamped / 60;
    int64_t const rem_secs = clamped % 60;
    return std::format("{}m {:02d}s", mins, rem_secs);
}

std::string format_remaining_duration(int64_t estimated_remaining_seconds) {
    if (estimated_remaining_seconds < 0) {
        return "estimating...";
    }
    if (estimated_remaining_seconds < 60) {
        return std::format("~{}s remaining", estimated_remaining_seconds);
    }
    int64_t const mins = estimated_remaining_seconds / 60;
    int64_t const rem_secs = estimated_remaining_seconds % 60;
    if (rem_secs == 0) {
        return std::format("~{}m remaining", mins);
    }
    return std::format("~{}m {:02d}s remaining", mins, rem_secs);
}

std::string format_progress_message_for_channel(
    const ProgressAssessment& assessment,
    [[maybe_unused]] std::string_view channel
) {
    std::string const elapsed_str = format_duration_compact(
        assessment.checkpoint_seconds > 0 ? assessment.checkpoint_seconds : assessment.elapsed_seconds
    );
    std::string const remaining_str = format_remaining_duration(assessment.estimated_remaining_seconds);

    std::string msg = std::format("⏳ [{} elapsed | {}] {}", elapsed_str, remaining_str, assessment.summary);
    if (!assessment.current_activity.empty() &&
        assessment.summary.find(assessment.current_activity) == std::string::npos) {
        msg += std::format(" (Step: {})", assessment.current_activity);
    }
    return msg;
}

ProgressAssessment build_fallback_progress_assessment(
    const ExecutionTelemetrySnapshot& snapshot
) {
    ProgressAssessment result{};
    result.elapsed_seconds = snapshot.elapsed_seconds;
    result.checkpoint_seconds = snapshot.checkpoint_seconds;
    result.completed_turns = snapshot.completed_turns;
    result.monitor_persona_name = PersonaManager::instance().get_progress_monitor_persona();
    result.from_llm = false;
    result.confidence = "low";

    std::string active_tool_name;
    std::string last_finished_tool_name;
    size_t finished_count = 0;

    for (const auto& t : snapshot.recent_tools) {
        if (t.is_running) {
            active_tool_name = t.tool_name;
            if (!t.summary.empty()) {
                active_tool_name += " (" + t.summary + ")";
            }
        } else {
            ++finished_count;
            last_finished_tool_name = t.tool_name;
        }
    }

    if (!snapshot.current_step.empty()) {
        result.current_activity = snapshot.current_step;
    } else if (!active_tool_name.empty()) {
        result.current_activity = active_tool_name;
    } else if (!last_finished_tool_name.empty()) {
        result.current_activity = "Completed " + last_finished_tool_name;
    } else {
        result.current_activity = "Reasoning and generating response";
    }

    // Heuristic remaining-duration estimate from live turn cadence
    int64_t const effective_elapsed = std::max<int64_t>(
        1,
        snapshot.elapsed_seconds > 0 ? snapshot.elapsed_seconds : snapshot.checkpoint_seconds
    );
    if (snapshot.completed_turns > 0) {
        int64_t const avg_per_turn = std::max<int64_t>(10, effective_elapsed / static_cast<int64_t>(snapshot.completed_turns));
        result.estimated_remaining_seconds = std::clamp<int64_t>(avg_per_turn * 2, 15, 180);
    } else {
        result.estimated_remaining_seconds = std::clamp<int64_t>(effective_elapsed, 30, 180);
    }

    std::string const persona_label = snapshot.active_persona_name.empty()
        ? "AI assistant"
        : snapshot.active_persona_name;

    if (!active_tool_name.empty()) {
        result.summary = std::format(
            "{} has completed {} internal turn(s) ({} tool call(s) finished) and is currently executing {}.",
            persona_label,
            snapshot.completed_turns,
            finished_count,
            active_tool_name
        );
    } else if (snapshot.completed_turns > 0 || finished_count > 0) {
        result.summary = std::format(
            "{} has completed {} internal turn(s) and is synthesizing results ({}).",
            persona_label,
            snapshot.completed_turns,
            result.current_activity
        );
    } else {
        result.summary = std::format(
            "{} is actively processing your request ({}).",
            persona_label,
            result.current_activity
        );
    }

    return result;
}

ProgressAssessment parse_progress_assessment_response(
    std::string_view raw_llm_output,
    const ExecutionTelemetrySnapshot& snapshot
) {
    std::string const json_slice = extract_json_object_slice(raw_llm_output);
    if (json_slice.empty()) {
        return build_fallback_progress_assessment(snapshot);
    }

    ProgressAssessmentWire wire{};
    auto const err = glz::read<glz::opts{.error_on_unknown_keys = false}>(wire, json_slice);
    if (err || wire.summary.empty()) {
        return build_fallback_progress_assessment(snapshot);
    }

    ProgressAssessment result{};
    result.summary = std::move(wire.summary);
    result.current_activity = !wire.current_activity.empty()
        ? std::move(wire.current_activity)
        : std::move(wire.current_step);
    if (result.current_activity.empty()) {
        result.current_activity = !snapshot.current_step.empty()
            ? snapshot.current_step
            : "Processing request";
    }
    result.estimated_remaining_seconds = wire.estimated_remaining_seconds >= 0
        ? wire.estimated_remaining_seconds
        : -1;

    std::string const lower_conf = to_lower_ascii(wire.confidence);
    if (lower_conf == "low" || lower_conf == "medium" || lower_conf == "high") {
        result.confidence = lower_conf;
    } else {
        result.confidence = "medium";
    }

    result.elapsed_seconds = snapshot.elapsed_seconds;
    result.checkpoint_seconds = snapshot.checkpoint_seconds;
    result.completed_turns = snapshot.completed_turns;
    result.monitor_persona_name = PersonaManager::instance().get_progress_monitor_persona();
    result.from_llm = true;
    return result;
}

bool is_timeout_error_message(std::string_view error_text) noexcept {
    std::string const lower = to_lower_ascii(error_text);
    return lower.find("timed out") != std::string::npos ||
           lower.find("timeout") != std::string::npos ||
           lower.find("curle_operation_timedout") != std::string::npos ||
           lower.find("deadline exceeded") != std::string::npos ||
           lower.find("gateway timeout") != std::string::npos ||
           lower.find("http error 504") != std::string::npos;
}

bool is_cancellation_error_message(std::string_view error_text) noexcept {
    std::string const lower = to_lower_ascii(error_text);
    return lower.find("cancelled by user") != std::string::npos ||
           lower.find("canceled by user") != std::string::npos ||
           lower.find("execution cancelled") != std::string::npos ||
           lower.find("execution canceled") != std::string::npos ||
           lower.find("request cancelled") != std::string::npos;
}

ErrorTransmissionReport build_error_transmission_report(
    std::string_view raw_error,
    const ExecutionTelemetrySnapshot& snapshot,
    ExecutionFailureKind explicit_kind
) {
    ErrorTransmissionReport report{};
    report.elapsed_seconds = snapshot.elapsed_seconds;
    report.completed_turns = snapshot.completed_turns;
    report.active_persona_name = snapshot.active_persona_name.empty() ? "AI Persona" : snapshot.active_persona_name;
    report.active_model_name = snapshot.active_model_name;
    report.raw_error = std::string(raw_error);

    if (!snapshot.current_step.empty()) {
        report.last_activity = snapshot.current_step;
    } else if (!snapshot.recent_tools.empty()) {
        report.last_activity = snapshot.recent_tools.back().tool_name;
    } else {
        report.last_activity = "LLM generation";
    }

    ExecutionFailureKind kind = explicit_kind;
    if (kind == ExecutionFailureKind::None) {
        if (is_cancellation_error_message(raw_error)) {
            kind = ExecutionFailureKind::Cancelled;
        } else if (is_timeout_error_message(raw_error)) {
            kind = ExecutionFailureKind::Timeout;
        } else if (raw_error.empty() || to_lower_ascii(raw_error).find("empty response") != std::string::npos) {
            kind = ExecutionFailureKind::EmptyResponse;
        } else {
            kind = ExecutionFailureKind::ProviderError;
        }
    }

    report.kind = kind;
    report.is_timeout = (kind == ExecutionFailureKind::Timeout);
    report.is_cancelled = (kind == ExecutionFailureKind::Cancelled);

    std::string const elapsed_label = format_duration_compact(snapshot.elapsed_seconds);
    std::string const model_suffix = report.active_model_name.empty()
        ? ""
        : std::format(" ({})", report.active_model_name);

    switch (kind) {
        case ExecutionFailureKind::Timeout:
            report.formatted_channel_message = std::format(
                "⏱️ [Timeout after {}] Request to persona '{}'{} timed out during '{}' after {} completed turn(s). Details: {}",
                elapsed_label,
                report.active_persona_name,
                model_suffix,
                report.last_activity,
                report.completed_turns,
                report.raw_error.empty() ? "Operation timed out." : report.raw_error
            );
            break;
        case ExecutionFailureKind::Cancelled:
            report.formatted_channel_message = std::format(
                "🛑 [Cancelled after {}] Request to persona '{}'{} was stopped during '{}'.",
                elapsed_label,
                report.active_persona_name,
                model_suffix,
                report.last_activity
            );
            break;
        case ExecutionFailureKind::EmptyResponse:
            report.formatted_channel_message = std::format(
                "⚠️ [Empty Response after {}] Persona '{}'{} completed {} turn(s) (last step: '{}') but returned no content. Please try again.",
                elapsed_label,
                report.active_persona_name,
                model_suffix,
                report.completed_turns,
                report.last_activity
            );
            break;
        case ExecutionFailureKind::None:
        case ExecutionFailureKind::ProviderError:
        case ExecutionFailureKind::UnhandledException:
            report.formatted_channel_message = std::format(
                "❌ [Error after {}] Request to persona '{}'{} failed during '{}': {}",
                elapsed_label,
                report.active_persona_name,
                model_suffix,
                report.last_activity,
                report.raw_error.empty() ? "Unknown execution error" : report.raw_error
            );
            break;
    }

    return report;
}

struct AIProgressMonitor::Impl {
    Options options;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::thread worker_thread;

    std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};
    bool running{false};
    bool stop_requested{false};
    bool outcome_reported{false};

    uint32_t completed_turns{0};
    std::string current_step{"Reasoning"};
    std::vector<ToolTelemetryEntry> recent_tools{};
    std::optional<ProgressAssessment> latest_assessment{};
    std::vector<ProgressAssessment> assessment_history{};

    explicit Impl(Options opts) : options(std::move(opts)) {
        if (options.second_duration.count() <= 0) {
            options.second_duration = std::chrono::milliseconds(1000);
        }
    }

    ~Impl() {
        stop();
    }

    void start() {
        std::lock_guard<std::mutex> lock(mutex);
        if (running) {
            return;
        }
        start_time = std::chrono::steady_clock::now();
        stop_requested = false;
        running = true;
        worker_thread = std::thread([this]() { timer_loop(); });
    }

    void stop() noexcept {
        std::thread to_join;
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop_requested = true;
            running = false;
            cv.notify_all();
            if (worker_thread.joinable()) {
                to_join = std::move(worker_thread);
            }
        }
        if (to_join.joinable()) {
            if (to_join.get_id() == std::this_thread::get_id()) {
                to_join.detach();
            } else {
                try {
                    to_join.join();
                } catch (...) {}
            }
        }
    }

    ExecutionTelemetrySnapshot snapshot_locked(int64_t checkpoint_secs = 0) const {
        ExecutionTelemetrySnapshot snap{};
        snap.channel_name = options.channel_name;
        snap.active_persona_name = options.active_persona_name;
        snap.active_model_name = options.active_model_name;
        snap.user_request = options.user_request;

        auto const now = std::chrono::steady_clock::now();
        auto const elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        auto const unit_ms = std::max<int64_t>(1, options.second_duration.count());
        snap.elapsed_seconds = std::max<int64_t>(checkpoint_secs, elapsed_ms / unit_ms);
        snap.checkpoint_seconds = checkpoint_secs;
        snap.completed_turns = completed_turns;
        snap.current_step = current_step;
        snap.recent_tools = recent_tools;
        snap.recent_conversation = options.recent_conversation;
        return snap;
    }

    std::string evaluate_with_monitor_persona(const ExecutionTelemetrySnapshot& snap) const {
        const auto& pm = PersonaManager::instance();
        std::string monitor_name = !options.monitor_persona_name.empty()
            ? options.monitor_persona_name
            : pm.get_progress_monitor_persona();
        const Persona& monitor_persona = pm.get_progress_monitor_persona_config();
        if (monitor_name.empty()) {
            monitor_name = monitor_persona.name;
        }

        std::string const sys_prompt = monitor_persona.system_prompt;
        std::string const usr_prompt = build_monitor_user_prompt(snap);

        try {
            if (options.custom_evaluator) {
                return options.custom_evaluator(sys_prompt, usr_prompt, monitor_name);
            }
            AssessmentEvaluator default_eval;
            {
                std::lock_guard<std::mutex> lock(default_eval_mutex());
                default_eval = default_eval_storage();
            }
            if (default_eval) {
                return default_eval(sys_prompt, usr_prompt, monitor_name);
            }
            auto reg_eval = registrar::try_get<std::function<std::string(const std::string&, const std::string&, const std::string&)>>(
                "ai_progress_monitor_eval"
            );
            if (reg_eval && *reg_eval) {
                return (*reg_eval)(sys_prompt, usr_prompt, monitor_name);
            }
        } catch (...) {}
        return {};
    }

    void timer_loop() {
        int64_t current_checkpoint = 0;
        while (true) {
            int64_t const next_checkpoint = next_progress_checkpoint_seconds(current_checkpoint);
            auto const target_time = start_time + (options.second_duration * next_checkpoint);

            ExecutionTelemetrySnapshot snap;
            ProgressCallback progress_cb;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait_until(lock, target_time, [this]() { return stop_requested; });
                if (stop_requested) {
                    return;
                }
                current_checkpoint = next_checkpoint;
                snap = snapshot_locked(current_checkpoint);
                progress_cb = options.on_progress;
            }

            std::string const raw_eval = evaluate_with_monitor_persona(snap);

            ProgressAssessment assessment = parse_progress_assessment_response(raw_eval, snap);
            assessment.checkpoint_seconds = current_checkpoint;
            if (assessment.elapsed_seconds < current_checkpoint) {
                assessment.elapsed_seconds = current_checkpoint;
            }

            {
                std::lock_guard<std::mutex> lock(mutex);
                if (stop_requested) {
                    return;
                }
                latest_assessment = assessment;
                assessment_history.push_back(assessment);
            }

            if (progress_cb) {
                try {
                    progress_cb(assessment);
                } catch (...) {}
            }
        }
    }
};

AIProgressMonitor::AIProgressMonitor(Options options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

AIProgressMonitor::~AIProgressMonitor() {
    if (impl_) {
        impl_->stop();
    }
}

void AIProgressMonitor::start() {
    impl_->start();
}

void AIProgressMonitor::stop() noexcept {
    impl_->stop();
}

void AIProgressMonitor::record_tool_start(std::string_view tool_name, std::string_view summary) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->current_step = std::string(tool_name);
    impl_->recent_tools.push_back(ToolTelemetryEntry{
        .tool_name = std::string(tool_name),
        .summary = std::string(summary),
        .is_running = true,
        .duration_ms = 0
    });
}

void AIProgressMonitor::record_tool_end(std::string_view tool_name, int64_t duration_ms) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto it = impl_->recent_tools.rbegin(); it != impl_->recent_tools.rend(); ++it) {
        if (it->tool_name == tool_name && it->is_running) {
            it->is_running = false;
            it->duration_ms = duration_ms;
            break;
        }
    }
    impl_->current_step = std::format("Completed {}", tool_name);
}

void AIProgressMonitor::record_turn_completed(uint32_t total_turns) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (total_turns > 0) {
        impl_->completed_turns = total_turns;
    } else {
        ++impl_->completed_turns;
    }
}

void AIProgressMonitor::set_current_step(std::string_view step) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->current_step = std::string(step);
}

void AIProgressMonitor::set_active_model(std::string_view model_name) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->options.active_model_name = std::string(model_name);
}

void AIProgressMonitor::mark_completed() noexcept {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->outcome_reported = true;
    }
    impl_->stop();
}

ErrorTransmissionReport AIProgressMonitor::notify_error_or_timeout(
    std::string_view error_message,
    ExecutionFailureKind explicit_kind
) {
    impl_->stop();

    ExecutionTelemetrySnapshot snap;
    ErrorCallback err_cb;
    bool should_invoke = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        snap = impl_->snapshot_locked(0);
        if (!impl_->outcome_reported) {
            impl_->outcome_reported = true;
            should_invoke = true;
            err_cb = impl_->options.on_error_or_timeout;
        }
    }

    ErrorTransmissionReport report = build_error_transmission_report(error_message, snap, explicit_kind);
    if (should_invoke && err_cb) {
        try {
            err_cb(report);
        } catch (...) {}
    }
    return report;
}

ErrorTransmissionReport AIProgressMonitor::notify_cancelled(std::string_view reason) {
    return notify_error_or_timeout(reason, ExecutionFailureKind::Cancelled);
}

void AIProgressMonitor::finalize_if_unreported(std::string_view fallback_error) {
    bool already_reported = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        already_reported = impl_->outcome_reported;
    }
    if (!already_reported) {
        notify_error_or_timeout(fallback_error, ExecutionFailureKind::UnhandledException);
    } else {
        impl_->stop();
    }
}

ExecutionTelemetrySnapshot AIProgressMonitor::get_telemetry_snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snapshot_locked(0);
}

std::optional<ProgressAssessment> AIProgressMonitor::get_latest_assessment() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->latest_assessment;
}

std::vector<ProgressAssessment> AIProgressMonitor::get_assessment_history() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->assessment_history;
}

void AIProgressMonitor::set_default_evaluator(AssessmentEvaluator evaluator) {
    std::lock_guard<std::mutex> lock(default_eval_mutex());
    default_eval_storage() = std::move(evaluator);
}

} // namespace rouen::helpers
