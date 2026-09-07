#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <glaze/json/json_t.hpp>
#include "quickjs.h"

namespace rouen::hosts {

// Execution safety constants
inline constexpr std::size_t quickjs_max_memory_bytes{32 * 1024 * 1024}; // 32 MB cap
inline constexpr std::chrono::seconds quickjs_script_timeout{3};          // 3 second CPU timeout guard

/**
 * Modern C++23 Stateful Host for embedded QuickJS execution in Rouen.
 * Manages JSRuntime, JSContext, Glaze JSON interop bridge, Rouen namespace bindings,
 * and execution timeout & memory guards.
 */
class quickjs_host {
public:
    static quickjs_host& instance();

    quickjs_host(const quickjs_host&) = delete;
    quickjs_host& operator=(const quickjs_host&) = delete;

    bool initialize();
    void shutdown();
    [[nodiscard]] bool is_initialized() const noexcept { return ctx_ != nullptr; }

    // Evaluates JS code string, returning result or stringified JSON / error message.
    std::string eval_script(std::string_view js_code, const char* filename = "script.js");

    // Invokes a global function in JS context passing Glaze JSON arguments.
    glz::json_t call_function(const std::string& func_name, const glz::json_t& args = {});

    // Main loop tick executor for pending microtasks / async JS jobs.
    void execute_pending_jobs();

    // Marshaling bridge functions: glz::json_t <-> JSValue
    static JSValue glaze_to_jsvalue(JSContext* ctx, const glz::json_t& json_obj);
    static glz::json_t jsvalue_to_glaze(JSContext* ctx, JSValue val);

private:
    quickjs_host() = default;
    ~quickjs_host();

    JSRuntime* rt_{nullptr};
    JSContext* ctx_{nullptr};

    std::chrono::steady_clock::time_point execution_start_time_{};
    mutable std::mutex eval_mutex_{};

    void bind_rouen_namespace();

    // Interrupt handler callback for CPU timeout enforcement
    static int js_interrupt_handler(JSRuntime* rt, void* opaque);

    // Native JS bindings
    static JSValue js_rouen_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_rouen_create_card(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_rouen_emit_event(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_rouen_on_event(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_rouen_list_plugins(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_rouen_call_plugin(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_rouen_fetch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    // Track active event bus subscription handlers created from JS
    struct js_event_subscription {
        uint64_t sub_id{0};
        JSValue js_callback{JS_UNDEFINED};
    };
    std::vector<js_event_subscription> event_subscriptions_{};
};

} // namespace rouen::hosts
