#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../helpers/glaze_include.hpp"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wc99-extensions"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wimplicit-int-float-conversion"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4100 4244 4267 4996)
#endif

#include "quickjs.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

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
        JSValue js_callback{};
    };
    std::vector<js_event_subscription> event_subscriptions_{};
};

} // namespace rouen::hosts
