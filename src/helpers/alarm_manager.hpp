#pragma once

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "adaptive_cards/parser.hpp"
#include "glaze_include.hpp"

namespace rouen::helpers {

struct alarm_record {
    std::string id;
    std::string source_uri;
    std::string severity{"high"};
    std::string title;
    std::string value;
    bool latched{true};
    bool condition_active{false};
    std::string state{"NORMAL"}; // NORMAL, UNACK_ALM, ACK_ALM, UNACK_RTN, SHELVED
    std::string raised_at_utc;
    std::string acknowledged_by;
    std::string acknowledged_at_utc;
    std::string shelved_by;
    std::string shelve_reason;
    std::chrono::steady_clock::time_point shelved_until{};
};

class alarm_manager {
public:
    using process_ack_callback_t = std::function<void(const std::string& action_json)>;

    static alarm_manager& instance() {
        static alarm_manager inst;
        return inst;
    }

    static std::string current_utc_iso8601() {
        auto const now = std::chrono::system_clock::now();
        std::time_t const t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_utc{};
#if defined(_WIN32)
        gmtime_s(&tm_utc, &t);
#else
        gmtime_r(&t, &tm_utc);
#endif
        char buf[32]{};
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
        return std::string(buf);
    }

    void register_process_callback(const std::string& source_uri, process_ack_callback_t cb) {
        std::lock_guard<std::mutex> const lock(mutex_);
        callbacks_[source_uri] = std::move(cb);
    }

    void unregister_process_callback(const std::string& source_uri) {
        std::lock_guard<std::mutex> const lock(mutex_);
        callbacks_.erase(source_uri);
    }

    void report_process_alarms(
        const std::string& source_uri,
        const std::vector<adaptive_cards::alarm_declaration>& active_declarations) {
        std::lock_guard<std::mutex> const lock(mutex_);
        check_expirations_locked();

        std::unordered_set<std::string> seen_ids;
        for (const auto& decl : active_declarations) {
            if (decl.id.empty()) {
                continue;
            }
            seen_ids.insert(decl.id);
            auto it = alarms_.find(decl.id);
            if (it == alarms_.end()) {
                alarm_record rec{};
                rec.id = decl.id;
                rec.source_uri = source_uri;
                rec.severity = decl.severity.empty() ? "high" : decl.severity;
                rec.title = decl.title;
                rec.value = decl.value;
                rec.latched = decl.latched;
                rec.condition_active = true;
                rec.state = "UNACK_ALM";
                rec.raised_at_utc = current_utc_iso8601();
                alarms_.emplace(rec.id, std::move(rec));
            } else {
                auto& rec = it->second;
                rec.source_uri = source_uri;
                rec.severity = decl.severity.empty() ? rec.severity : decl.severity;
                rec.title = decl.title;
                rec.value = decl.value;
                rec.latched = decl.latched;
                rec.condition_active = true;

                if (rec.state == "SHELVED") {
                    // Remain shelved until timer expires
                } else if (rec.state == "NORMAL" || rec.state == "UNACK_RTN") {
                    rec.state = "UNACK_ALM";
                    rec.raised_at_utc = current_utc_iso8601();
                    rec.acknowledged_by.clear();
                    rec.acknowledged_at_utc.clear();
                }
                // If ACK_ALM or UNACK_ALM, remain in that state while condition is active
            }
        }

        // Check existing alarms for this source_uri whose physical condition cleared
        for (auto& [id, rec] : alarms_) {
            if (rec.source_uri == source_uri && !seen_ids.contains(id) && rec.condition_active) {
                rec.condition_active = false;
                if (rec.state == "UNACK_ALM") {
                    rec.state = rec.latched ? "UNACK_RTN" : "NORMAL";
                } else if (rec.state == "ACK_ALM") {
                    rec.state = "NORMAL";
                }
            }
        }
    }

    bool acknowledge(const std::string& alarm_id, const std::string& operator_email) {
        process_ack_callback_t cb_to_invoke;
        std::string payload_to_send;
        {
            std::lock_guard<std::mutex> const lock(mutex_);
            auto it = alarms_.find(alarm_id);
            if (it == alarms_.end()) {
                return false;
            }
            auto& rec = it->second;
            rec.acknowledged_by = operator_email;
            rec.acknowledged_at_utc = current_utc_iso8601();
            if (rec.state == "UNACK_ALM") {
                rec.state = rec.condition_active ? "ACK_ALM" : "NORMAL";
            } else if (rec.state == "UNACK_RTN") {
                rec.state = "NORMAL";
            } else if (rec.state == "SHELVED" && !rec.condition_active) {
                rec.state = "NORMAL";
            }

            auto cb_it = callbacks_.find(rec.source_uri);
            if (cb_it != callbacks_.end() && cb_it->second) {
                cb_to_invoke = cb_it->second;
                glz::json_t msg = glz::json_t::object_t{};
                msg["verb"] = "ack_alarm";
                msg["alarm_id"] = alarm_id;
                msg["acknowledged_by"] = operator_email;
                (void)glz::write_json(msg, payload_to_send);
            }
        }
        if (cb_to_invoke && !payload_to_send.empty()) {
            cb_to_invoke(payload_to_send);
        }
        return true;
    }

    bool shelve(
        const std::string& alarm_id,
        int duration_seconds,
        const std::string& reason,
        const std::string& operator_email) {
        std::lock_guard<std::mutex> const lock(mutex_);
        auto it = alarms_.find(alarm_id);
        if (it == alarms_.end()) {
            return false;
        }
        auto& rec = it->second;
        rec.state = "SHELVED";
        rec.shelved_by = operator_email;
        rec.shelve_reason = reason;
        rec.shelved_until = std::chrono::steady_clock::now() + std::chrono::seconds(std::max(1, duration_seconds));
        return true;
    }

    void check_expirations() {
        std::lock_guard<std::mutex> const lock(mutex_);
        check_expirations_locked();
    }

    [[nodiscard]] std::optional<alarm_record> get_alarm(const std::string& alarm_id) {
        std::lock_guard<std::mutex> const lock(mutex_);
        check_expirations_locked();
        auto it = alarms_.find(alarm_id);
        if (it == alarms_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] std::vector<alarm_record> list_alarms(bool include_normal = false) {
        std::lock_guard<std::mutex> const lock(mutex_);
        check_expirations_locked();
        std::vector<alarm_record> out;
        for (const auto& [_, rec] : alarms_) {
            if (!include_normal && rec.state == "NORMAL") {
                continue;
            }
            out.push_back(rec);
        }
        auto severity_rank = [](const std::string& sev) -> int {
            if (sev == "critical") return 0;
            if (sev == "high") return 1;
            if (sev == "medium" || sev == "warning") return 2;
            return 3;
        };
        std::ranges::sort(out, [&](const alarm_record& a, const alarm_record& b) {
            int const ra = severity_rank(a.severity);
            int const rb = severity_rank(b.severity);
            if (ra != rb) return ra < rb;
            return a.id < b.id;
        });
        return out;
    }

    [[nodiscard]] std::string to_json(bool include_normal = true) {
        auto const list = list_alarms(include_normal);
        glz::json_t arr = glz::json_t::array_t{};
        for (const auto& rec : list) {
            glz::json_t item = glz::json_t::object_t{};
            item["id"] = rec.id;
            item["source_uri"] = rec.source_uri;
            item["severity"] = rec.severity;
            item["title"] = rec.title;
            item["value"] = rec.value;
            item["latched"] = rec.latched;
            item["condition_active"] = rec.condition_active;
            item["state"] = rec.state;
            item["raised_at_utc"] = rec.raised_at_utc;
            item["acknowledged_by"] = rec.acknowledged_by;
            item["acknowledged_at_utc"] = rec.acknowledged_at_utc;
            item["shelved_by"] = rec.shelved_by;
            item["shelve_reason"] = rec.shelve_reason;
            arr.get<glz::json_t::array_t>().push_back(std::move(item));
        }
        glz::json_t root = glz::json_t::object_t{};
        root["alarms"] = std::move(arr);
        std::string out = "{}";
        (void)glz::write_json(root, out);
        return out;
    }

    void clear_all() {
        std::lock_guard<std::mutex> const lock(mutex_);
        alarms_.clear();
        callbacks_.clear();
    }

private:
    void check_expirations_locked() {
        auto const now = std::chrono::steady_clock::now();
        for (auto& [_, rec] : alarms_) {
            if (rec.state == "SHELVED" && now >= rec.shelved_until) {
                rec.state = rec.condition_active ? "UNACK_ALM" : "NORMAL";
                rec.shelved_by.clear();
                rec.shelve_reason.clear();
            }
        }
    }

    std::mutex mutex_;
    std::map<std::string, alarm_record> alarms_;
    std::map<std::string, process_ack_callback_t> callbacks_;
};

} // namespace rouen::helpers
