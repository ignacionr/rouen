#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "../hosts/rouen_mesh_host.hpp"
#include "../registrar.hpp"
#include "config_service.hpp"
#include "debug.hpp"
#include "glaze_include.hpp"

namespace rouen::services {

/**
 * Presence Record DTO stored in the Rouen Mesh Registry.
 * Used for inferring where a user was last actively interacting across mesh clients.
 */
struct presence_record {
    std::string client_id;
    std::string user;
    std::string hostname;
    std::string platform{"mac"};
    uint64_t last_active_epoch_ms{0};
    std::string last_active_iso;
    std::string interaction_type{"ui"}; // "ui_input", "mouse", "keyboard", "detached_window", "api", "startup"
    std::string status{"active"};        // "active", "idle"
};

/**
 * Summary DTO written to key "presence/last_active" in the mesh registry.
 */
struct last_active_summary {
    std::string client_id;
    std::string user;
    uint64_t last_active_epoch_ms{0};
    std::string last_active_iso;
    std::string interaction_type{"ui"};
};

/**
 * Request payload for POST /api/presence/touch.
 */
struct presence_touch_request {
    std::string interaction_type{"api_touch"};
};

/**
 * Presence Service
 * Keeps the Rouen mesh registry updated as to where a user was last interacting
 * (on which mesh client), enabling presence inference and smart notification targeting.
 */
class presence_service {
public:
    static presence_service& instance() {
        static presence_service inst;
        return inst;
    }

    presence_service(const presence_service&) = delete;
    presence_service& operator=(const presence_service&) = delete;

    void start() {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (running_.load()) return;

        running_.store(true);
        auto now_sys = std::chrono::system_clock::now();
        last_interaction_time_ = now_sys;
        last_interaction_epoch_ms_ = current_epoch_ms();
        last_interaction_iso_ = current_iso_time(now_sys);
        last_published_steady_ = std::chrono::steady_clock::time_point{};
        last_registry_refresh_steady_ = std::chrono::steady_clock::now();

        // Publish initial presence on startup
        publish_presence("startup", true);

        worker_thread_ = std::make_unique<std::thread>(&presence_service::worker_loop, this);
        SYS_DEBUG("[PresenceService] Started user presence service");
    }

    void stop() {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (!running_.load()) return;

        running_.store(false);
        worker_cv_.notify_all();
        if (worker_thread_ && worker_thread_->joinable()) {
            worker_thread_->join();
        }
        worker_thread_.reset();
        SYS_DEBUG("[PresenceService] Stopped user presence service");
    }

    /**
     * Record a user interaction on the local client (e.g. mouse movement, keystroke, window focus, API call).
     * Automatically debounces registry publishing to avoid network congestion.
     */
    void record_interaction(std::string_view interaction_type = "ui_input") {
        auto now_sys = std::chrono::system_clock::now();
        auto now_steady = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            last_interaction_time_ = now_sys;
            last_interaction_epoch_ms_ = current_epoch_ms(now_sys);
            last_interaction_iso_ = current_iso_time(now_sys);
            last_interaction_type_ = std::string(interaction_type);
            is_idle_reported_ = false;
        }

        uint64_t ms_since_pub = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            now_steady - last_published_steady_).count());

        if (ms_since_pub >= min_publish_interval_ms_) {
            publish_presence(interaction_type, false);
        } else {
            needs_publish_.store(true);
        }
    }

    /**
     * Force or perform a presence publication to the Rouen mesh registry.
     */
    void publish_presence(std::string_view interaction_type = "ui_input", bool force = false) {
        auto now_steady = std::chrono::steady_clock::now();
        if (!force) {
            uint64_t ms_since_pub = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                now_steady - last_published_steady_).count());
            if (ms_since_pub < min_publish_interval_ms_) {
                needs_publish_.store(true);
                return;
            }
        }

        presence_record local_rec = get_local_presence();
        local_rec.interaction_type = std::string(interaction_type);

        std::string rec_json;
        if (glz::write_json(local_rec, rec_json) == glz::error_code::none) {
            auto& mesh = hosts::rouen_mesh_host::instance();
            // Publish per-client record: presence/{client_id}
            mesh.set_registry_value(std::format("presence/{}", local_rec.client_id), rec_json, true);

            // Publish globally tracked last_active record
            last_active_summary summary{
                .client_id = local_rec.client_id,
                .user = local_rec.user,
                .last_active_epoch_ms = local_rec.last_active_epoch_ms,
                .last_active_iso = local_rec.last_active_iso,
                .interaction_type = local_rec.interaction_type
            };
            std::string summary_json;
            if (glz::write_json(summary, summary_json) == glz::error_code::none) {
                mesh.set_registry_value("presence/last_active", summary_json, true);
            }
        }

        last_published_steady_ = now_steady;
        needs_publish_.store(false);
    }

    /**
     * Get the presence record for this local client.
     */
    [[nodiscard]] presence_record get_local_presence() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return presence_record{
            .client_id = get_local_client_id(),
            .user = detect_current_user(),
            .hostname = detect_hostname(),
            .platform = "mac",
            .last_active_epoch_ms = last_interaction_epoch_ms_,
            .last_active_iso = last_interaction_iso_,
            .interaction_type = last_interaction_type_,
            .status = is_locally_idle_internal() ? "idle" : "active"
        };
    }

    /**
     * Get the local mesh client ID.
     */
    [[nodiscard]] std::string get_local_client_id() const {
        auto cfg = hosts::rouen_mesh_host::instance().get_config();
        if (!cfg.client_id.empty()) {
            return cfg.client_id;
        }
        return "rouen-client";
    }

    /**
     * Infer the presence record where the user was last active across all mesh nodes.
     */
    [[nodiscard]] presence_record get_last_active_presence() const {
        auto& mesh = hosts::rouen_mesh_host::instance();
        presence_record best_record = get_local_presence();

        // 1. Try checking "presence/last_active" in mesh registry
        auto last_active_val = mesh.get_registry_value("presence/last_active");
        if (last_active_val && !last_active_val->empty()) {
            last_active_summary summary{};
            if (glz::read_json(summary, *last_active_val) == glz::error_code::none && !summary.client_id.empty()) {
                if (summary.last_active_epoch_ms >= best_record.last_active_epoch_ms) {
                    best_record.client_id = summary.client_id;
                    best_record.user = summary.user;
                    best_record.last_active_epoch_ms = summary.last_active_epoch_ms;
                    best_record.last_active_iso = summary.last_active_iso;
                    best_record.interaction_type = summary.interaction_type;
                }
            }
        }

        // 2. Cross-check all "presence/" entries to find newest interaction
        auto entries = mesh.get_registry_entries("presence/");
        for (const auto& [k, entry] : entries) {
            if (k == "presence/last_active") continue;
            if (entry.value.empty()) continue;
            presence_record rec{};
            if (glz::read_json(rec, entry.value) == glz::error_code::none && !rec.client_id.empty()) {
                if (rec.last_active_epoch_ms > best_record.last_active_epoch_ms) {
                    best_record = rec;
                }
            }
        }

        return best_record;
    }

    /**
     * Get the mesh client ID where the user was last active.
     */
    [[nodiscard]] std::string get_last_active_client_id() const {
        return get_last_active_presence().client_id;
    }

    /**
     * Returns true if the user was last interacting on THIS local mesh client.
     */
    [[nodiscard]] bool is_local_client_last_active() const {
        return get_last_active_client_id() == get_local_client_id();
    }

    /**
     * Returns the recommended notification target client ID.
     * If the user is active on another node, returns that remote node ID; otherwise returns local client ID.
     */
    [[nodiscard]] std::string get_recommended_notification_target() const {
        return get_last_active_client_id();
    }

    /**
     * Returns true if notifications should be handled/alerted on this local client.
     */
    [[nodiscard]] bool should_notify_locally() const {
        return is_local_client_last_active() || !is_locally_idle();
    }

    /**
     * Retrieve all presence records known across the Rouen mesh registry.
     */
    [[nodiscard]] std::vector<presence_record> get_all_presences() const {
        auto& mesh = hosts::rouen_mesh_host::instance();
        std::vector<presence_record> result;
        auto entries = mesh.get_registry_entries("presence/");
        bool local_included = false;
        std::string local_id = get_local_client_id();

        for (const auto& [k, entry] : entries) {
            if (k == "presence/last_active") continue;
            if (entry.value.empty()) continue;
            presence_record rec{};
            if (glz::read_json(rec, entry.value) == glz::error_code::none && !rec.client_id.empty()) {
                if (rec.client_id == local_id) {
                    local_included = true;
                }
                result.push_back(rec);
            }
        }

        if (!local_included) {
            result.push_back(get_local_presence());
        }

        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return a.last_active_epoch_ms > b.last_active_epoch_ms;
        });

        return result;
    }

    /**
     * Number of seconds elapsed since user last interacted on this local machine.
     */
    [[nodiscard]] uint64_t get_seconds_since_last_interaction() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto now = std::chrono::system_clock::now();
        if (now < last_interaction_time_) return 0;
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
            now - last_interaction_time_).count());
    }

    /**
     * Check whether the local user is considered idle (no interaction within idle threshold).
     */
    [[nodiscard]] bool is_locally_idle(uint64_t threshold_sec = 300) const {
        return get_seconds_since_last_interaction() >= threshold_sec;
    }

    static std::string detect_current_user() {
        const char* u = std::getenv("USER");
        if (!u || !*u) u = std::getenv("USERNAME");
        if (!u || !*u) u = "user";
        return std::string(u);
    }

    static std::string detect_hostname() {
#ifndef _WIN32
        char buf[256];
        if (gethostname(buf, sizeof(buf)) == 0) {
            buf[sizeof(buf) - 1] = '\0';
            return std::string(buf);
        }
#endif
        return "mac";
    }

    static std::string current_iso_time(std::chrono::system_clock::time_point tp = std::chrono::system_clock::now()) {
        return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(tp));
    }

    static uint64_t current_epoch_ms(std::chrono::system_clock::time_point tp = std::chrono::system_clock::now()) {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()).count());
    }

private:
    presence_service() {
        auto now = std::chrono::system_clock::now();
        last_interaction_time_ = now;
        last_interaction_epoch_ms_ = current_epoch_ms(now);
        last_interaction_iso_ = current_iso_time(now);
    }

    ~presence_service() {
        stop();
    }

    bool is_locally_idle_internal() const {
        auto now = std::chrono::system_clock::now();
        if (now < last_interaction_time_) return false;
        return std::chrono::duration_cast<std::chrono::seconds>(now - last_interaction_time_).count() >= 300;
    }

    void worker_loop() {
        while (running_.load()) {
            std::unique_lock<std::mutex> lock(worker_mutex_);
            worker_cv_.wait_for(lock, std::chrono::seconds(5), [this] {
                return !running_.load();
            });
            if (!running_.load()) break;

            auto now_steady = std::chrono::steady_clock::now();

            // 1. If interaction was recorded and debounce timer expired, publish
            if (needs_publish_.load()) {
                uint64_t ms_since_pub = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    now_steady - last_published_steady_).count());
                if (ms_since_pub >= min_publish_interval_ms_) {
                    std::string itype;
                    {
                        std::lock_guard<std::mutex> state_lock(state_mutex_);
                        itype = last_interaction_type_;
                    }
                    publish_presence(itype, false);
                }
            }

            // 2. Periodically refresh mesh registry entries (every 30 seconds)
            uint64_t ms_since_refresh = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                now_steady - last_registry_refresh_steady_).count());
            if (ms_since_refresh >= 30000) {
                hosts::rouen_mesh_host::instance().refresh_registry();
                last_registry_refresh_steady_ = now_steady;
            }

            // 3. Transition to idle state if inactive for >= 300 seconds
            if (!is_idle_reported_ && is_locally_idle()) {
                publish_presence("idle_transition", true);
                is_idle_reported_ = true;
            }
        }
    }

    std::mutex lifecycle_mutex_;
    mutable std::mutex state_mutex_;
    std::mutex worker_mutex_;
    std::condition_variable worker_cv_;

    std::atomic<bool> running_{false};
    std::atomic<bool> needs_publish_{false};
    bool is_idle_reported_{false};

    uint64_t min_publish_interval_ms_{5000}; // Debounce interval (5 seconds)

    std::chrono::system_clock::time_point last_interaction_time_{};
    uint64_t last_interaction_epoch_ms_{0};
    std::string last_interaction_iso_;
    std::string last_interaction_type_{"ui_input"};

    std::chrono::steady_clock::time_point last_published_steady_{};
    std::chrono::steady_clock::time_point last_registry_refresh_steady_{};

    std::unique_ptr<std::thread> worker_thread_;
};

} // namespace rouen::services

namespace rouen::helpers {
    using presence_service = rouen::services::presence_service;
    using presence_record = rouen::services::presence_record;
}
