#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/fetch.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../hosts/rouen_mesh_host.hpp"
#include "../../registrar.hpp"
#include "card.hpp"

namespace rouen::cards {

// 1. URI Parsing and Canonicalization Helpers
struct mesh_uri_info {
    std::string client_id;
    std::string target_card_uri;
    bool is_navigator{false};
    bool is_mesh_console{false};
};

inline std::optional<mesh_uri_info> parse_mesh_uri(std::string_view uri) {
    if (uri.empty()) return std::nullopt;

    // Check for bracket sugar syntax: [client-id]optional-card-uri
    if (uri.starts_with('[')) {
        auto close_pos = uri.find(']');
        if (close_pos != std::string_view::npos && close_pos > 1) {
            std::string client_id(uri.substr(1, close_pos - 1));
            std::string_view rest = uri.substr(close_pos + 1);
            if (rest.starts_with('/')) {
                rest.remove_prefix(1);
            }
            if (rest.empty()) {
                return mesh_uri_info{
                    .client_id = std::move(client_id),
                    .target_card_uri = "",
                    .is_navigator = true,
                    .is_mesh_console = false
                };
            } else {
                return mesh_uri_info{
                    .client_id = std::move(client_id),
                    .target_card_uri = std::string(rest),
                    .is_navigator = false,
                    .is_mesh_console = false
                };
            }
        }
    }

    if (uri == "mesh" || uri == "mesh:") {
        return mesh_uri_info{
            .client_id = "",
            .target_card_uri = "",
            .is_navigator = false,
            .is_mesh_console = true
        };
    }

    std::string_view locator;
    if (uri.starts_with("mesh://")) {
        locator = uri.substr(7);
    } else if (uri.starts_with("mesh:")) {
        locator = uri.substr(5);
        if (locator.starts_with("//")) {
            locator.remove_prefix(2);
        }
    } else {
        return std::nullopt;
    }

    if (locator.empty()) {
        return mesh_uri_info{
            .client_id = "",
            .target_card_uri = "",
            .is_navigator = false,
            .is_mesh_console = true
        };
    }

    auto slash_pos = locator.find('/');
    if (slash_pos == std::string_view::npos) {
        return mesh_uri_info{
            .client_id = std::string(locator),
            .target_card_uri = "",
            .is_navigator = true,
            .is_mesh_console = false
        };
    }

    std::string client_id(locator.substr(0, slash_pos));
    std::string_view rest = locator.substr(slash_pos + 1);
    if (rest.empty()) {
        return mesh_uri_info{
            .client_id = std::move(client_id),
            .target_card_uri = "",
            .is_navigator = true,
            .is_mesh_console = false
        };
    }

    return mesh_uri_info{
        .client_id = std::move(client_id),
        .target_card_uri = std::string(rest),
        .is_navigator = false,
        .is_mesh_console = false
    };
}

inline std::string canonicalize_mesh_uri(std::string_view uri) {
    auto parsed = parse_mesh_uri(uri);
    if (!parsed) return std::string(uri);
    if (parsed->is_mesh_console) return "mesh";
    if (parsed->is_navigator) return std::format("mesh://{}", parsed->client_id);
    return std::format("mesh://{}/{}", parsed->client_id, parsed->target_card_uri);
}

// 2. Remote Card DTO & Parser
struct remote_open_card_dto {
    int index{0};
    std::string title;
    std::string uri;
    std::string adaptive_card_json;
};

inline std::vector<remote_open_card_dto> parse_remote_cards_response(const std::string& json_str) {
    std::vector<remote_open_card_dto> results;
    if (json_str.empty()) return results;

    glz::json_t doc;
    if (glz::read_json(doc, json_str) != glz::error_code::none) {
        return results;
    }

    auto extract_card = [](const glz::json_t& item) -> std::optional<remote_open_card_dto> {
        if (!item.holds<glz::json_t::object_t>()) return std::nullopt;
        const auto& obj = item.get<glz::json_t::object_t>();

        remote_open_card_dto dto;
        if (auto it = obj.find("index"); it != obj.end()) {
            if (it->second.holds<double>()) dto.index = static_cast<int>(it->second.get<double>());
        }
        if (auto it = obj.find("title"); it != obj.end() && it->second.holds<std::string>()) {
            dto.title = it->second.get<std::string>();
        }
        if (auto it = obj.find("uri"); it != obj.end() && it->second.holds<std::string>()) {
            dto.uri = it->second.get<std::string>();
        }
        if (auto it = obj.find("adaptive_card"); it != obj.end()) {
            if (it->second.holds<std::string>()) {
                dto.adaptive_card_json = it->second.get<std::string>();
            } else {
                std::string out;
                (void)glz::write_json(it->second, out);
                if (out != "null") dto.adaptive_card_json = std::move(out);
            }
        }
        return dto;
    };

    if (doc.holds<glz::json_t::array_t>()) {
        const auto& arr = doc.get<glz::json_t::array_t>();
        for (const auto& item : arr) {
            if (auto card_opt = extract_card(item)) {
                results.push_back(std::move(*card_opt));
            }
        }
    } else if (doc.holds<glz::json_t::object_t>()) {
        const auto& obj = doc.get<glz::json_t::object_t>();
        if (auto it = obj.find("cards"); it != obj.end() && it->second.holds<glz::json_t::array_t>()) {
            const auto& arr = it->second.get<glz::json_t::array_t>();
            for (const auto& item : arr) {
                if (auto card_opt = extract_card(item)) {
                    results.push_back(std::move(*card_opt));
                }
            }
        }
    }

    return results;
}

// 3. Scheme Discovery & Caching
inline std::vector<std::string> parse_remote_schemas_response(const std::string& json_str) {
    std::vector<std::string> schemas;
    if (json_str.empty()) return schemas;

    glz::json_t doc;
    if (glz::read_json(doc, json_str) != glz::error_code::none) {
        return schemas;
    }

    if (doc.holds<glz::json_t::array_t>()) {
        const auto& arr = doc.get<glz::json_t::array_t>();
        for (const auto& item : arr) {
            if (item.holds<std::string>()) {
                schemas.push_back(item.get<std::string>());
            }
        }
    }
    std::sort(schemas.begin(), schemas.end());
    return schemas;
}

class peer_schemas_cache {
public:
    static peer_schemas_cache& instance() {
        static peer_schemas_cache inst;
        return inst;
    }

    void set_schemas(const std::string& client_id, std::vector<std::string> schemas) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_[client_id] = std::move(schemas);
    }

    std::vector<std::string> get_schemas(const std::string& client_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(client_id);
        if (it != cache_.end()) return it->second;
        return {};
    }

    bool has_schemas(const std::string& client_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.find(client_id) != cache_.end();
    }

    std::vector<std::string> filter_schemas(const std::string& client_id, std::string_view filter) const {
        std::vector<std::string> res;
        auto all = get_schemas(client_id);
        if (filter.empty()) return all;
        std::string filter_lower = ::helpers::StringHelper::to_lower(filter);
        for (const auto& s : all) {
            if (::helpers::StringHelper::to_lower(s).find(filter_lower) != std::string::npos) {
                res.push_back(s);
            }
        }
        return res;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }

private:
    peer_schemas_cache() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<std::string>> cache_;
};

// 4. Action Request JSON Builder
inline std::string build_action_request_json(const std::string& target_uri, const std::string& action_payload) {
    glz::json_t body;
    body["uri"] = target_uri;
    glz::json_t action_obj;
    if (glz::read_json(action_obj, action_payload) == glz::error_code::none) {
        body["action"] = action_obj;
    } else {
        body["action"] = action_payload;
    }
    std::string out;
    (void)glz::write_json(body, out);
    return out;
}

// 5. Remote Card Status
enum class remote_card_status {
    connecting,
    spawning,
    active,
    action_in_flight,
    reconnecting,
    failed
};

// 6. mesh_card_proxy
class mesh_card_proxy : public card {
public:
    mesh_card_proxy(std::string_view target_client_id, std::string_view target_card_uri, bool auto_start = true)
        : target_client_id_(target_client_id), target_card_uri_(target_card_uri) {
        colors[0] = {0.18f, 0.42f, 0.72f, 1.0f};
        colors[1] = {0.12f, 0.30f, 0.52f, 0.75f};
        window_title = std::format("[{}] {}", target_client_id_, target_card_uri_);
        name(window_title);
        width = 540.0f;

        if (auto_start) {
            ensure_virtual_route();
            start_worker();
        }
    }

    ~mesh_card_proxy() override {
        is_stopping_ = true;
        worker_cv_.notify_all();
        if (sync_thread_ && sync_thread_->joinable()) {
            sync_thread_->join();
        }
    }

    std::string get_uri() const override {
        return std::format("mesh://{}/{}", target_client_id_, target_card_uri_);
    }

    bool matches_uri(std::string_view uri) const override {
        auto parsed = parse_mesh_uri(uri);
        return parsed.has_value() && !parsed->is_navigator && !parsed->is_mesh_console &&
               parsed->client_id == target_client_id_ && parsed->target_card_uri == target_card_uri_;
    }

    void handle_uri(std::string_view /*uri*/) override {
        force_sync_now_ = true;
    }

    std::string get_adaptive_card_json() const override {
        std::lock_guard<std::mutex> lock(proxy_mutex_);
        return raw_card_json_;
    }

    void handle_action(std::string_view action_json) override {
        dispatch_action_async(std::string(action_json));
    }

    // JSON diffing: returns true if parsed/updated, false if identical
    bool update_adaptive_json_if_changed(const std::string& new_json) {
        std::lock_guard<std::mutex> lock(proxy_mutex_);
        if (new_json == raw_card_json_ && !bound_doc_.body.empty()) {
            return false;
        }
        try {
            bound_doc_ = helpers::adaptive_cards::parser{}.parse(new_json);
            raw_card_json_ = new_json;
            status_ = remote_card_status::active;
            status_error_message_.clear();
            return true;
        } catch (const std::exception& ex) {
            status_error_message_ = ex.what();
            return false;
        }
    }

    bool render() override {
        return render_window([this]() {
            render_status_header();
            ImGui::Separator();

            if (status_ == remote_card_status::reconnecting || status_ == remote_card_status::failed) {
                render_offline_overlay();
                return;
            }

            if (status_ == remote_card_status::connecting || status_ == remote_card_status::spawning) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "Connecting to [%s] via Rouen Mesh...", target_client_id_.c_str());
                ImGui::Spacing();
                if (ImGui::Button("🔄 Retry Connection")) {
                    force_sync_now_ = true;
                }
                return;
            }

            std::lock_guard<std::mutex> lock(proxy_mutex_);
            if (!bound_doc_.body.empty()) {
                renderer_.render(
                    bound_doc_,
                    input_state_,
                    helpers::adaptive_cards::renderer::action_callbacks{
                        .open_url = [](const std::string& url) {
                            rouen::platform::open_url(url);
                        },
                        .on_submit = [this](const std::string& payload) {
                            dispatch_action_async(payload);
                        }
                    },
                    helpers::adaptive_cards::render_config{
                        .font_bold = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                        .font_code = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                    },
                    [](const std::string&, int&, int&) -> RouenGPUTexture* {
                        return nullptr;
                    }
                );
            } else {
                ImGui::TextDisabled("Waiting for remote card definition...");
            }
        });
    }

    void ensure_virtual_route() {
        std::lock_guard<std::mutex> lock(proxy_mutex_);
        auto routes = rouen::hosts::rouen_mesh_host::instance().get_active_routes();
        for (const auto& r : routes) {
            if (r.target_client_id == target_client_id_ && r.target_port == 8081 && r.local_port != 0) {
                local_proxy_url_ = std::format("http://127.0.0.1:{}", r.local_port);
                active_route_id_ = r.route_id;
                return;
            }
        }
        std::string err;
        if (rouen::hosts::rouen_mesh_host::instance().open_virtual_route(target_client_id_, 8081, err, 0, false)) {
            auto updated_routes = rouen::hosts::rouen_mesh_host::instance().get_active_routes();
            for (const auto& r : updated_routes) {
                if (r.target_client_id == target_client_id_ && r.target_port == 8081 && r.local_port != 0) {
                    local_proxy_url_ = std::format("http://127.0.0.1:{}", r.local_port);
                    active_route_id_ = r.route_id;
                    return;
                }
            }
        } else {
            status_ = remote_card_status::failed;
            status_error_message_ = err;
        }
    }

private:
    void trigger_sync() {
        force_sync_now_ = true;
        worker_cv_.notify_one();
    }

    void start_worker() {
        last_sync_time_ = std::chrono::steady_clock::now();
        sync_thread_ = std::make_unique<std::thread>([this]() {
            sync_card_state();
            while (!is_stopping_) {
                {
                    std::unique_lock<std::mutex> lock(worker_mutex_);
                    worker_cv_.wait_for(lock, sync_interval_, [this]() {
                        return is_stopping_.load() || force_sync_now_.load();
                    });
                }
                if (is_stopping_) break;
                force_sync_now_ = false;
                sync_card_state();
            }
        });
    }

    void sync_card_state() {
        if (local_proxy_url_.empty()) {
            ensure_virtual_route();
            if (local_proxy_url_.empty()) return;
        }

        is_syncing_ = true;
        auto start_tp = std::chrono::steady_clock::now();
        std::string encoded_uri = ::helpers::StringHelper::url_encode(target_card_uri_);
        std::string url = std::format("{}/api/cards/adaptive?uri={}", local_proxy_url_, encoded_uri);

        try {
            http::fetch client(4);
            std::string resp = client(url);
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_tp).count();
            last_ping_ms_ = static_cast<uint32_t>(elapsed);

            glz::json_t doc;
            if (glz::read_json(doc, resp) == glz::error_code::none && doc.holds<glz::json_t::object_t>()) {
                const auto& obj = doc.get<glz::json_t::object_t>();
                
                // If remote deck doesn't have the card open, spawn it via POST /api/cards
                if (obj.find("error") != obj.end()) {
                    std::string create_url = std::format("{}/api/cards", local_proxy_url_);
                    std::string create_body = std::format(R"({{"uri":"{}"}})", target_card_uri_);
                    try {
                        client.post(create_url, create_body, {{"Content-Type", "application/json"}});
                        resp = client(url);
                        (void)glz::read_json(doc, resp);
                    } catch (...) {}
                }

                if (doc.holds<glz::json_t::object_t>()) {
                    const auto& obj2 = doc.get<glz::json_t::object_t>();
                    if (auto it = obj2.find("title"); it != obj2.end() && it->second.holds<std::string>()) {
                        std::string remote_title = it->second.get<std::string>();
                        if (!remote_title.empty()) {
                            window_title = std::format("[{}] {}", target_client_id_, remote_title);
                            name(window_title);
                        }
                    }
                    if (auto it = obj2.find("adaptive_card"); it != obj2.end()) {
                        std::string card_json;
                        if (it->second.holds<std::string>()) {
                            card_json = it->second.get<std::string>();
                        } else {
                            (void)glz::write_json(it->second, card_json);
                        }
                        if (!card_json.empty() && card_json != "null") {
                            update_adaptive_json_if_changed(card_json);
                        }
                    }
                }
            } else {
                update_adaptive_json_if_changed(resp);
            }
            if (status_ == remote_card_status::connecting || status_ == remote_card_status::reconnecting) {
                status_ = remote_card_status::active;
            }
        } catch (const std::exception& ex) {
            status_ = remote_card_status::reconnecting;
            status_error_message_ = ex.what();
        }
        is_syncing_ = false;
    }

    void dispatch_action_async(std::string action_payload) {
        status_ = remote_card_status::action_in_flight;
        std::thread([this, payload = std::move(action_payload)]() {
            if (local_proxy_url_.empty()) {
                ensure_virtual_route();
            }
            if (!local_proxy_url_.empty()) {
                try {
                    http::fetch client(5);
                    std::string action_url = std::format("{}/api/cards/action", local_proxy_url_);
                    std::string body = build_action_request_json(target_card_uri_, payload);
                    client.post(action_url, body, {{"Content-Type", "application/json"}});
                } catch (const std::exception& ex) {
                    status_error_message_ = ex.what();
                }
            }
            sync_card_state();
        }).detach();
    }

    void render_status_header() {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "NODE: %s", target_client_id_.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", target_card_uri_.c_str());

        std::string badge;
        ImVec4 badge_col;
        if (status_ == remote_card_status::action_in_flight) {
            badge = "🟡 Syncing...";
            badge_col = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        } else if (status_ == remote_card_status::connecting || status_ == remote_card_status::spawning) {
            badge = "🟡 Connecting...";
            badge_col = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        } else if (status_ == remote_card_status::active) {
            uint32_t ping = last_ping_ms_.load();
            if (ping < 100) {
                badge = std::format("🟢 {}ms", ping);
                badge_col = ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
            } else {
                badge = std::format("🟡 {}ms", ping);
                badge_col = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
            }
        } else {
            badge = "🔴 Disconnected";
            badge_col = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
        }

        float badge_width = ImGui::CalcTextSize(badge.c_str()).x;
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > badge_width + 10.0f) {
            ImGui::SameLine(ImGui::GetCursorPosX() + avail - badge_width);
        }
        ImGui::TextColored(badge_col, "%s", badge.c_str());
    }

    void render_offline_overlay() {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::Text("⚠️ Remote Host Disconnected");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::TextWrapped("Lost connection to mesh node \"%s\".", target_client_id_.c_str());
        if (!status_error_message_.empty()) {
            ImGui::TextDisabled("Details: %s", status_error_message_.c_str());
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Auto-reconnecting every 2s...");
        ImGui::Spacing();
        if (ImGui::Button("🔄 Reconnect Now")) {
            status_ = remote_card_status::connecting;
            status_error_message_.clear();
            force_sync_now_ = true;
        }
        ImGui::Spacing();
    }

    std::string target_client_id_;
    std::string target_card_uri_;
    std::string local_proxy_url_;
    uint32_t active_route_id_{0};

    remote_card_status status_{remote_card_status::connecting};
    std::string status_error_message_;
    std::atomic<uint32_t> last_ping_ms_{0};

    std::string raw_card_json_;
    helpers::adaptive_cards::card_document bound_doc_{};
    mutable helpers::adaptive_cards::renderer::input_state input_state_{};
    helpers::adaptive_cards::renderer renderer_{};

    std::chrono::steady_clock::time_point last_sync_time_{};
    std::chrono::milliseconds sync_interval_{2000};
    std::atomic<bool> is_syncing_{false};
    std::atomic<bool> is_stopping_{false};
    std::atomic<bool> force_sync_now_{false};
    mutable std::mutex proxy_mutex_;
    std::mutex worker_mutex_;
    std::condition_variable worker_cv_;
    std::unique_ptr<std::thread> sync_thread_;
};

// 7. mesh_node_navigator
class mesh_node_navigator : public card {
public:
    explicit mesh_node_navigator(std::string_view target_client_id, bool auto_refresh = true)
        : target_client_id_(target_client_id) {
        colors[0] = {0.18f, 0.48f, 0.58f, 1.0f};
        colors[1] = {0.12f, 0.34f, 0.42f, 0.75f};
        window_title = std::format("🌐 Remote Node: {}", target_client_id_);
        name(window_title);
        width = 620.0f;

        if (auto_refresh) {
            refresh_async();
        }
    }

    std::string get_uri() const override {
        return std::format("mesh://{}", target_client_id_);
    }

    bool matches_uri(std::string_view uri) const override {
        auto parsed = parse_mesh_uri(uri);
        return parsed.has_value() && parsed->is_navigator && parsed->client_id == target_client_id_;
    }

    void handle_uri(std::string_view /*uri*/) override {
        refresh_async();
    }

    bool render() override {
        return render_window([this]() {
            render_navigator_ui();
        });
    }

    void refresh_async() {
        if (is_refreshing_.exchange(true)) return;
        std::thread([this]() {
            ensure_virtual_route();
            if (!local_proxy_url_.empty()) {
                auto start_tp = std::chrono::steady_clock::now();
                http::fetch client(4);
                try {
                    // 1. Fetch active cards
                    std::string cards_url = std::format("{}/api/cards/adaptive", local_proxy_url_);
                    std::string cards_resp = client(cards_url);
                    auto cards = parse_remote_cards_response(cards_resp);
                    
                    // 2. Fetch schemas
                    std::string schemas_url = std::format("{}/api/schemas", local_proxy_url_);
                    std::string schemas_resp = client(schemas_url);
                    auto schemas = parse_remote_schemas_response(schemas_resp);

                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_tp).count();
                    last_ping_ms_ = static_cast<uint32_t>(elapsed);

                    std::lock_guard<std::mutex> lock(nav_mutex_);
                    active_cards_ = std::move(cards);
                    schemas_ = std::move(schemas);
                    peer_schemas_cache::instance().set_schemas(target_client_id_, schemas_);
                    error_message_.clear();
                } catch (const std::exception& ex) {
                    std::lock_guard<std::mutex> lock(nav_mutex_);
                    error_message_ = ex.what();
                }
            }
            is_refreshing_ = false;
        }).detach();
    }

private:
    void ensure_virtual_route() {
        std::lock_guard<std::mutex> lock(nav_mutex_);
        auto routes = rouen::hosts::rouen_mesh_host::instance().get_active_routes();
        for (const auto& r : routes) {
            if (r.target_client_id == target_client_id_ && r.target_port == 8081 && r.local_port != 0) {
                local_proxy_url_ = std::format("http://127.0.0.1:{}", r.local_port);
                return;
            }
        }
        std::string err;
        if (rouen::hosts::rouen_mesh_host::instance().open_virtual_route(target_client_id_, 8081, err, 0, false)) {
            auto updated_routes = rouen::hosts::rouen_mesh_host::instance().get_active_routes();
            for (const auto& r : updated_routes) {
                if (r.target_client_id == target_client_id_ && r.target_port == 8081 && r.local_port != 0) {
                    local_proxy_url_ = std::format("http://127.0.0.1:{}", r.local_port);
                    return;
                }
            }
        } else {
            error_message_ = err;
        }
    }

    void render_navigator_ui() {
        // Header
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "REMOTE NODE: %s", target_client_id_.c_str());
        ImGui::SameLine();

        uint32_t ping = last_ping_ms_.load();
        if (!error_message_.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "🔴 Disconnected");
        } else if (ping > 0) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "🟢 %ums", ping);
        } else {
            ImGui::TextDisabled("🟡 Probing...");
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 85.0f);
        if (ImGui::Button("🔄 Sync") && !is_refreshing_) {
            refresh_async();
        }

        ImGui::Separator();

        if (!error_message_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Route error: %s", error_message_.c_str());
            ImGui::Spacing();
        }

        std::lock_guard<std::mutex> lock(nav_mutex_);
        if (ImGui::BeginTabBar("RemoteNodeTabs")) {
            // Tab 1: Active Deck Cards
            std::string active_title = std::format("Active Deck Cards ({})", active_cards_.size());
            if (ImGui::BeginTabItem(active_title.c_str())) {
                if (active_cards_.empty()) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("No active cards reported on %s.", target_client_id_.c_str());
                } else {
                    ImGui::Spacing();
                    for (const auto& c : active_cards_) {
                        ImGui::PushID(c.index);
                        ImGui::BeginGroup();
                        ImGui::TextColored(ImVec4(0.9f, 0.9f, 1.0f, 1.0f), "#%d  %s", c.index, c.title.c_str());
                        ImGui::TextDisabled("URI: %s", c.uri.c_str());
                        ImGui::EndGroup();

                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 140.0f);
                        if (ImGui::Button("Mirror to Local Deck")) {
                            auto create_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                            if (create_fn && *create_fn) {
                                (*create_fn)(std::format("mesh://{}/{}", target_client_id_, c.uri));
                            }
                        }
                        ImGui::Separator();
                        ImGui::PopID();
                    }
                }
                ImGui::EndTabItem();
            }

            // Tab 2: Registered Schemes
            std::string schemes_title = std::format("Registered Schemes ({})", schemas_.size());
            if (ImGui::BeginTabItem(schemes_title.c_str())) {
                ImGui::Spacing();
                
                // Quick Launch section
                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.4f, 1.0f), "QUICK LAUNCH ON %s:", target_client_id_.c_str());
                static char quick_buf[256] = "";
                ImGui::PushItemWidth(300.0f);
                ImGui::InputTextWithHint("##quick_launch", "e.g. sysinfo, vcproject:/c/work", quick_buf, sizeof(quick_buf));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("🚀 Launch New Card")) {
                    std::string target_uri = quick_buf;
                    if (!target_uri.empty()) {
                        auto create_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                        if (create_fn && *create_fn) {
                            (*create_fn)(std::format("mesh://{}/{}", target_client_id_, target_uri));
                        }
                    }
                }

                ImGui::Separator();

                // Schemes list
                static char filter_buf[128] = "";
                ImGui::InputTextWithHint("##filter_schemas", "Filter schemas...", filter_buf, sizeof(filter_buf));
                ImGui::Spacing();

                auto filtered = peer_schemas_cache::instance().filter_schemas(target_client_id_, filter_buf);
                if (filtered.empty()) {
                    ImGui::TextDisabled("No schemes match the filter.");
                } else {
                    for (const auto& s : filtered) {
                        ImGui::PushID(s.c_str());
                        ImGui::Text("• %s", s.c_str());
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70.0f);
                        if (ImGui::Button("Launch")) {
                            auto create_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                            if (create_fn && *create_fn) {
                                (*create_fn)(std::format("mesh://{}/{}", target_client_id_, s));
                            }
                        }
                        ImGui::PopID();
                    }
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    std::string target_client_id_;
    std::string local_proxy_url_;
    std::vector<remote_open_card_dto> active_cards_;
    std::vector<std::string> schemas_;
    std::atomic<bool> is_refreshing_{false};
    std::atomic<uint32_t> last_ping_ms_{0};
    std::string error_message_;
    mutable std::mutex nav_mutex_;
};

} // namespace rouen::cards
