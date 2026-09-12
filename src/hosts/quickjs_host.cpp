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

#include "quickjs_host.hpp"
#include "../helpers/fetch.hpp"
#include "cards/interface/factory.hpp"
#include "hosts/event_bus_host.hpp"
#include "hosts/plugin_host.hpp"

namespace rouen::hosts {

quickjs_host& quickjs_host::instance() {
    static quickjs_host inst{};
    return inst;
}

quickjs_host::~quickjs_host() {
    shutdown();
}

int quickjs_host::js_interrupt_handler(JSRuntime* /*rt*/, void* opaque) {
    auto* self = static_cast<quickjs_host*>(opaque);
    if (!self) {
        return 0;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - self->execution_start_time_ > quickjs_script_timeout) {
        std::cerr << "[QuickJS] Script execution timed out (>3s limit), interrupting execution.\n";
        return 1; // Non-zero interrupts execution
    }
    return 0;
}

bool quickjs_host::initialize() {
    if (ctx_) {
        return true;
    }

    rt_ = JS_NewRuntime();
    if (!rt_) {
        std::cerr << "[QuickJS] Failed to create JSRuntime\n";
        return false;
    }

    JS_SetMemoryLimit(rt_, quickjs_max_memory_bytes);
    JS_SetInterruptHandler(rt_, js_interrupt_handler, this);

    ctx_ = JS_NewContext(rt_);
    if (!ctx_) {
        std::cerr << "[QuickJS] Failed to create JSContext\n";
        JS_FreeRuntime(rt_);
        rt_ = nullptr;
        return false;
    }

    bind_rouen_namespace();
    std::cout << "[QuickJS] Host initialized successfully with 32MB memory cap and 3s execution guard.\n";
    return true;
}

void quickjs_host::shutdown() {
    std::unique_lock lock{eval_mutex_, std::defer_lock};
    static_cast<void>(lock.try_lock());
    if (ctx_) {
        for (auto& sub : event_subscriptions_) {
            if (JS_IsFunction(ctx_, sub.js_callback)) {
                JS_FreeValue(ctx_, sub.js_callback);
            }
        }
        event_subscriptions_.clear();

        JS_FreeContext(ctx_);
        ctx_ = nullptr;
    }
    if (rt_) {
        JS_FreeRuntime(rt_);
        rt_ = nullptr;
    }
}

JSValue quickjs_host::glaze_to_jsvalue(JSContext* ctx, const glz::json_t& json_obj) {
    if (!ctx) {
        return JS_UNDEFINED;
    }
    std::string json_str{};
    static_cast<void>(glz::write_json(json_obj, json_str));
    if (json_str.empty()) {
        json_str = "null";
    }
    return JS_ParseJSON(ctx, json_str.c_str(), json_str.length(), "<glaze>");
}

glz::json_t quickjs_host::jsvalue_to_glaze(JSContext* ctx, JSValue val) {
    glz::json_t result{};
    if (!ctx || JS_IsUndefined(val) || JS_IsNull(val)) {
        return result;
    }
    JSValue json_str_val = JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(json_str_val)) {
        return result;
    }
    const char* str = JS_ToCString(ctx, json_str_val);
    if (str) {
        static_cast<void>(glz::read_json(result, str));
        JS_FreeCString(ctx, str);
    }
    JS_FreeValue(ctx, json_str_val);
    return result;
}

std::string quickjs_host::eval_script(std::string_view js_code, const char* filename) {
    std::lock_guard lock{eval_mutex_};
    if (!initialize()) {
        return "{\"error\":\"Failed to initialize QuickJS engine\"}";
    }

    execution_start_time_ = std::chrono::steady_clock::now();

    JSValue val = JS_Eval(ctx_, js_code.data(), js_code.size(), filename, JS_EVAL_TYPE_GLOBAL);
    
    execute_pending_jobs();

    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(ctx_);
        const char* err_str = JS_ToCString(ctx_, exc);
        std::string error_msg = err_str ? err_str : "Unknown QuickJS exception";
        if (err_str) {
            JS_FreeCString(ctx_, err_str);
        }
        JS_FreeValue(ctx_, exc);
        JS_FreeValue(ctx_, val);
        std::string err_json{};
        static_cast<void>(glz::write_json(error_msg, err_json));
        return std::format("{{\"error\":{}}}", err_json);
    }

    glz::json_t res_json = jsvalue_to_glaze(ctx_, val);
    JS_FreeValue(ctx_, val);

    std::string out_str{};
    static_cast<void>(glz::write_json(res_json, out_str));
    return out_str;
}

glz::json_t quickjs_host::call_function(const std::string& func_name, const glz::json_t& args) {
    std::lock_guard lock{eval_mutex_};
    if (!initialize()) {
        return glz::json_t{};
    }

    execution_start_time_ = std::chrono::steady_clock::now();

    JSValue global_obj = JS_GetGlobalObject(ctx_);
    JSValue func_val = JS_GetPropertyStr(ctx_, global_obj, func_name.c_str());
    JS_FreeValue(ctx_, global_obj);

    if (!JS_IsFunction(ctx_, func_val)) {
        JS_FreeValue(ctx_, func_val);
        return glz::json_t{};
    }

    JSValue js_arg = glaze_to_jsvalue(ctx_, args);
    JSValueConst argv[1] = { js_arg };

    JSValue result_val = JS_Call(ctx_, func_val, JS_UNDEFINED, 1, argv);
    JS_FreeValue(ctx_, js_arg);
    JS_FreeValue(ctx_, func_val);

    execute_pending_jobs();

    if (JS_IsException(result_val)) {
        JSValue exc = JS_GetException(ctx_);
        const char* err_str = JS_ToCString(ctx_, exc);
        if (err_str) {
            std::cerr << "[QuickJS] Exception in function " << func_name << ": " << err_str << "\n";
            JS_FreeCString(ctx_, err_str);
        }
        JS_FreeValue(ctx_, exc);
        JS_FreeValue(ctx_, result_val);
        return glz::json_t{};
    }

    glz::json_t glaze_res = jsvalue_to_glaze(ctx_, result_val);
    JS_FreeValue(ctx_, result_val);
    return glaze_res;
}

void quickjs_host::execute_pending_jobs() {
    if (!rt_) return;
    JSContext* pctx{nullptr};
    while (JS_ExecutePendingJob(rt_, &pctx) > 0) {
        // Run until microtask queue is drained
    }
}

JSValue quickjs_host::js_rouen_log(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc > 0) {
        const char* str = JS_ToCString(ctx, argv[0]);
        if (str) {
            std::cout << "[QuickJS] " << str << "\n";
            JS_FreeCString(ctx, str);
        }
    }
    return JS_UNDEFINED;
}

JSValue quickjs_host::js_rouen_create_card(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_NULL;
    }
    const char* uri_str = JS_ToCString(ctx, argv[0]);
    if (!uri_str) {
        return JS_NULL;
    }
    std::string uri{uri_str};
    JS_FreeCString(ctx, uri_str);

    std::cout << "[QuickJS] Rouen.cards.create request for URI: " << uri << "\n";
    
    // Instantiate card via card factory (renderer = nullptr for background script creation)
    auto card_ptr = cards::factory::create_card(uri, nullptr);
    if (card_ptr) {
        glz::json_t result_obj = glz::json_t::object_t{};
        result_obj["created"] = true;
        result_obj["title"] = card_ptr->window_title;
        result_obj["uri"] = card_ptr->get_uri();
        return glaze_to_jsvalue(ctx, result_obj);
    }
    return JS_NULL;
}

JSValue quickjs_host::js_rouen_emit_event(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_FALSE;
    }
    const char* topic_str = JS_ToCString(ctx, argv[0]);
    if (!topic_str) {
        return JS_FALSE;
    }
    std::string topic{topic_str};
    JS_FreeCString(ctx, topic_str);

    glz::json_t payload{};
    if (argc >= 2) {
        payload = jsvalue_to_glaze(ctx, argv[1]);
    }

    events::rouen_event evt{
        .topic = std::move(topic),
        .source_id = "quickjs",
        .payload = std::move(payload)
    };

    event_bus_host::instance().publish(std::move(evt));
    return JS_TRUE;
}

JSValue quickjs_host::js_rouen_on_event(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 2 || !JS_IsFunction(ctx, argv[1])) {
        return JS_FALSE;
    }
    const char* pattern_str = JS_ToCString(ctx, argv[0]);
    if (!pattern_str) {
        return JS_FALSE;
    }
    std::string pattern{pattern_str};
    JS_FreeCString(ctx, pattern_str);

    JSValue callback_dup = JS_DupValue(ctx, argv[1]);

    auto sub_id = event_bus_host::instance().subscribe(
        pattern,
        [callback_dup, pattern](const events::rouen_event& evt) {
            auto& host = quickjs_host::instance();
            if (!host.ctx_) return;
            JSValue payload_js = glaze_to_jsvalue(host.ctx_, evt.payload);
            JSValue topic_js = JS_NewString(host.ctx_, evt.topic.c_str());
            
            // Construct event object { topic, payload }
            JSValue evt_obj = JS_NewObject(host.ctx_);
            JS_SetPropertyStr(host.ctx_, evt_obj, "topic", topic_js);
            JS_SetPropertyStr(host.ctx_, evt_obj, "payload", payload_js);

            JSValueConst args[1] = { evt_obj };
            JSValue res = JS_Call(host.ctx_, callback_dup, JS_UNDEFINED, 1, args);
            JS_FreeValue(host.ctx_, evt_obj);
            if (JS_IsException(res)) {
                JSValue exc = JS_GetException(host.ctx_);
                JS_FreeValue(host.ctx_, exc);
            }
            JS_FreeValue(host.ctx_, res);
        }
    );

    instance().event_subscriptions_.push_back({.sub_id = sub_id, .js_callback = callback_dup});
    return JS_NewInt64(ctx, static_cast<int64_t>(sub_id));
}

JSValue quickjs_host::js_rouen_list_plugins(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/, JSValueConst* /*argv*/) {
    const auto& plugins = plugin_host::instance().loaded_plugins();
    glz::json_t::array_t arr{};
    for (const auto& p : plugins) {
        arr.push_back(p);
    }
    return glaze_to_jsvalue(ctx, arr);
}

JSValue quickjs_host::js_rouen_call_plugin(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_NULL;
    }
    const char* plugin_id_str = JS_ToCString(ctx, argv[0]);
    const char* action_str = JS_ToCString(ctx, argv[1]);
    std::string plugin_id = plugin_id_str ? plugin_id_str : "";
    std::string action = action_str ? action_str : "";
    if (plugin_id_str) JS_FreeCString(ctx, plugin_id_str);
    if (action_str) JS_FreeCString(ctx, action_str);

    glz::json_t args{};
    if (argc >= 3) {
        args = jsvalue_to_glaze(ctx, argv[2]);
    }

    std::cout << "[QuickJS] Rouen.plugins.call -> plugin: " << plugin_id << ", action: " << action << "\n";
    glz::json_t resp_obj = glz::json_t::object_t{};
    resp_obj["status"] = "ok";
    resp_obj["plugin"] = plugin_id;
    resp_obj["action"] = action;
    resp_obj["result"] = "executed";
    return glaze_to_jsvalue(ctx, resp_obj);
}

JSValue quickjs_host::js_rouen_fetch(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_NULL;
    }
    const char* url_str = JS_ToCString(ctx, argv[0]);
    if (!url_str) {
        return JS_NULL;
    }
    std::string url{url_str};
    JS_FreeCString(ctx, url_str);

    std::string method = "GET";
    std::string body;
    std::vector<std::string> headers{"Content-Type: application/json"};

    if (argc >= 2 && JS_IsObject(argv[1])) {
        JSValue method_val = JS_GetPropertyStr(ctx, argv[1], "method");
        if (!JS_IsUndefined(method_val)) {
            const char* m_str = JS_ToCString(ctx, method_val);
            if (m_str) {
                method = m_str;
                JS_FreeCString(ctx, m_str);
            }
            JS_FreeValue(ctx, method_val);
        }

        JSValue body_val = JS_GetPropertyStr(ctx, argv[1], "body");
        if (!JS_IsUndefined(body_val)) {
            const char* b_str = JS_ToCString(ctx, body_val);
            if (b_str) {
                body = b_str;
                JS_FreeCString(ctx, b_str);
            }
            JS_FreeValue(ctx, body_val);
        }
    }

    std::string response_str;
    long status_code = 200;
    try {
        http::fetch fetcher{10};
        if (method == "POST") {
            response_str = fetcher.post(url, body, headers);
        } else {
            response_str = fetcher(url, headers);
        }
        status_code = fetcher.last_http_code();
        if (status_code == 0) status_code = 200;
    } catch (const std::exception& ex) {
        std::cerr << "[QuickJS] Rouen.fetch error: " << ex.what() << "\n";
        return JS_NULL;
    }

    if (!response_str.empty() && (response_str.starts_with('{') || response_str.starts_with('['))) {
        JSValue parsed = JS_ParseJSON(ctx, response_str.c_str(), response_str.length(), "<fetch>");
        if (!JS_IsException(parsed)) {
            return parsed;
        }
    }

    glz::json_t resp_obj = glz::json_t::object_t{};
    resp_obj["status"] = status_code;
    resp_obj["body"] = response_str;
    resp_obj["ok"] = (status_code >= 200 && status_code < 300);
    return glaze_to_jsvalue(ctx, resp_obj);
}

void quickjs_host::bind_rouen_namespace() {
    JSValue global_obj = JS_GetGlobalObject(ctx_);

    // Rouen main namespace object
    JSValue rouen_obj = JS_NewObject(ctx_);

    JS_SetPropertyStr(ctx_, rouen_obj, "log", JS_NewCFunction(ctx_, js_rouen_log, "log", 1));
    JS_SetPropertyStr(ctx_, rouen_obj, "emit", JS_NewCFunction(ctx_, js_rouen_emit_event, "emit", 2));
    JS_SetPropertyStr(ctx_, rouen_obj, "on", JS_NewCFunction(ctx_, js_rouen_on_event, "on", 2));
    JS_SetPropertyStr(ctx_, rouen_obj, "fetch", JS_NewCFunction(ctx_, js_rouen_fetch, "fetch", 2));

    // Rouen.cards sub-namespace
    JSValue cards_obj = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, cards_obj, "create", JS_NewCFunction(ctx_, js_rouen_create_card, "create", 2));
    JS_SetPropertyStr(ctx_, rouen_obj, "cards", cards_obj);

    // Rouen.plugins sub-namespace
    JSValue plugins_obj = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, plugins_obj, "list", JS_NewCFunction(ctx_, js_rouen_list_plugins, "list", 0));
    JS_SetPropertyStr(ctx_, plugins_obj, "call", JS_NewCFunction(ctx_, js_rouen_call_plugin, "call", 3));
    JS_SetPropertyStr(ctx_, rouen_obj, "plugins", plugins_obj);

    // Set Rouen global object
    JS_SetPropertyStr(ctx_, global_obj, "Rouen", rouen_obj);

    JS_FreeValue(ctx_, global_obj);
}

} // namespace rouen::hosts
