#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <format>
#include <optional>
#include <memory>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <mutex>

#include "../../helpers/glaze_include.hpp"
#include "../../helpers/platform_utils.hpp"

namespace rouen::models::productivity {

    struct objective_record {
        int id{0};
        std::optional<int> parent_id;
        std::string period;            // "quarterly", "monthly", "weekly", "daily"
        std::string period_identifier; // e.g. "2026-Q3", "2026-07", "2026-W28", "2026-07-06"
        std::string title;
        std::string type;              // "binary", "volumetric", "constraint"
        double target_val{0.0};
        double current_val{0.0};
        std::string status;            // "pending", "committed", "completed", "failed", "dropped"
        std::string created_at;
        std::string updated_at;

        struct glaze {
            using T = objective_record;
            static constexpr auto value = glz::object(
                "id", &T::id,
                "parent_id", &T::parent_id,
                "period", &T::period,
                "period_identifier", &T::period_identifier,
                "title", &T::title,
                "type", &T::type,
                "target_val", &T::target_val,
                "current_val", &T::current_val,
                "status", &T::status,
                "created_at", &T::created_at,
                "updated_at", &T::updated_at
            );
        };
    };

    struct ledger_record {
        std::string date;
        int closed{0};
        std::string closed_at;

        struct glaze {
            using T = ledger_record;
            static constexpr auto value = glz::object(
                "date", &T::date,
                "closed", &T::closed,
                "closed_at", &T::closed_at
            );
        };
    };

    struct date_context {
        std::string date;
        std::string week;
        std::string month;
        std::string quarter;
    };

    class objective_repository {
    public:
        explicit objective_repository(const std::string& base_path = "") {
            if (base_path.empty()) {
                dir_path_ = rouen::platform::get_user_data_path("objectives", true);
            } else {
                dir_path_ = std::filesystem::path(base_path);
                std::filesystem::create_directories(dir_path_);
            }
            load_data();
        }

        static date_context get_date_context_for_time(std::time_t time) {
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &time);
#else
            localtime_r(&time, &tm);
#endif
            date_context ctx;
            
            // Format YYYY-MM-DD
            char buf_date[32];
            std::strftime(buf_date, sizeof(buf_date), "%Y-%m-%d", &tm);
            ctx.date = buf_date;

            // Format YYYY-Www (ISO week)
            char buf_week[32];
            std::strftime(buf_week, sizeof(buf_week), "%G-W%V", &tm);
            ctx.week = buf_week;

            // Format YYYY-MM
            char buf_month[32];
            std::strftime(buf_month, sizeof(buf_month), "%Y-%m", &tm);
            ctx.month = buf_month;

            // Format YYYY-Q#
            int quarter = (tm.tm_mon / 3) + 1;
            ctx.quarter = std::format("{}-Q{}", tm.tm_year + 1900, quarter);

            return ctx;
        }

        static date_context get_current_date_context() {
            auto now = std::chrono::system_clock::now();
            return get_date_context_for_time(std::chrono::system_clock::to_time_t(now));
        }

        static date_context get_tomorrow_date_context() {
            auto now = std::chrono::system_clock::now();
            auto tomorrow = now + std::chrono::hours(24);
            return get_date_context_for_time(std::chrono::system_clock::to_time_t(tomorrow));
        }

        static date_context get_yesterday_date_context() {
            auto now = std::chrono::system_clock::now();
            auto yesterday = now - std::chrono::hours(24);
            return get_date_context_for_time(std::chrono::system_clock::to_time_t(yesterday));
        }

        std::vector<objective_record> get_objectives(const std::string& period, const std::string& identifier) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<objective_record> results;
            for (const auto& rec : objectives_) {
                if (rec.period == period && rec.period_identifier == identifier) {
                    results.push_back(rec);
                }
            }
            return results;
        }

        objective_record get_objective_by_id(int id) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& rec : objectives_) {
                if (rec.id == id) {
                    return rec;
                }
            }
            objective_record empty_rec;
            empty_rec.id = 0;
            return empty_rec;
        }

        int add_objective(const objective_record& rec) {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Find next unique ID
            int next_id = 1;
            for (const auto& o : objectives_) {
                if (o.id >= next_id) {
                    next_id = o.id + 1;
                }
            }

            objective_record new_rec = rec;
            new_rec.id = next_id;
            
            auto now = std::chrono::system_clock::now();
            std::string now_str = std::format("{:%F %T}", now);
            new_rec.created_at = now_str;
            new_rec.updated_at = now_str;

            objectives_.push_back(new_rec);
            save_data_unlocked();
            return next_id;
        }

        void update_objective(const objective_record& rec) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& o : objectives_) {
                if (o.id == rec.id) {
                    o.parent_id = rec.parent_id;
                    o.title = rec.title;
                    o.type = rec.type;
                    o.target_val = rec.target_val;
                    o.current_val = rec.current_val;
                    o.status = rec.status;
                    
                    auto now = std::chrono::system_clock::now();
                    o.updated_at = std::format("{:%F %T}", now);
                    break;
                }
            }
            save_data_unlocked();
        }

        void delete_objective(int id) {
            std::lock_guard<std::mutex> lock(mutex_);
            delete_objective_recursive(id);
            save_data_unlocked();
        }

        // Ledger States for Closing Days
        bool is_day_closed(const std::string& date) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& entry : ledger_) {
                if (entry.date == date) {
                    return entry.closed != 0;
                }
            }
            return false;
        }

        bool has_ledger_entry(const std::string& date) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& entry : ledger_) {
                if (entry.date == date) {
                    return true;
                }
            }
            return false;
        }

        void initialize_day_ledger(const std::string& date) {
            std::lock_guard<std::mutex> lock(mutex_);
            bool exists = false;
            for (const auto& entry : ledger_) {
                if (entry.date == date) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                ledger_record entry;
                entry.date = date;
                entry.closed = 0;
                entry.closed_at = "";
                ledger_.push_back(entry);
                save_data_unlocked();
            }
        }

        std::string get_unclosed_day_before(const std::string& current_date) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::string unclosed_date = "";
            for (const auto& entry : ledger_) {
                if (entry.date < current_date && entry.closed == 0) {
                    if (unclosed_date.empty() || entry.date > unclosed_date) {
                        unclosed_date = entry.date;
                    }
                }
            }
            return unclosed_date;
        }

        void quick_zero_and_archive_day(const std::string& date) {
            std::lock_guard<std::mutex> lock(mutex_);
            // Update all active objectives of that day to failed/completed depending on values
            for (auto& item : objectives_) {
                if (item.period == "daily" && item.period_identifier == date) {
                    bool is_success = false;
                    if (item.type == "binary") {
                        is_success = (item.current_val >= 1.0);
                    } else if (item.type == "volumetric") {
                        is_success = (item.current_val >= item.target_val);
                    } else if (item.type == "constraint") {
                        is_success = (item.current_val <= item.target_val);
                    }

                    item.status = is_success ? "completed" : "failed";
                    auto now = std::chrono::system_clock::now();
                    item.updated_at = std::format("{:%F %T}", now);
                }
            }
            
            // Mark day as closed in ledger
            bool found = false;
            auto now = std::chrono::system_clock::now();
            std::string now_str = std::format("{:%F %T}", now);
            for (auto& entry : ledger_) {
                if (entry.date == date) {
                    entry.closed = 1;
                    entry.closed_at = now_str;
                    found = true;
                    break;
                }
            }
            if (!found) {
                ledger_record entry;
                entry.date = date;
                entry.closed = 1;
                entry.closed_at = now_str;
                ledger_.push_back(entry);
            }
            
            save_data_unlocked();
        }

        void commit_day_objectives(const std::string& date) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& item : objectives_) {
                if (item.period == "daily" && item.period_identifier == date && item.status == "pending") {
                    item.status = "committed";
                    auto now = std::chrono::system_clock::now();
                    item.updated_at = std::format("{:%F %T}", now);
                }
            }
            
            bool ledger_exists = false;
            for (const auto& entry : ledger_) {
                if (entry.date == date) {
                    ledger_exists = true;
                    break;
                }
            }
            if (!ledger_exists) {
                ledger_record entry;
                entry.date = date;
                entry.closed = 0;
                entry.closed_at = "";
                ledger_.push_back(entry);
            }
            
            save_data_unlocked();
        }

        void close_day(const std::string& date, const std::vector<std::pair<int, std::string>>& rollovers) {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Collect items that need rolling over to create tomorrow
            std::vector<objective_record> new_rollover_items;
            
            for (auto& item : objectives_) {
                if (item.period == "daily" && item.period_identifier == date) {
                    bool is_success = false;
                    if (item.type == "binary") {
                        is_success = (item.current_val >= 1.0);
                    } else if (item.type == "volumetric") {
                        is_success = (item.current_val >= item.target_val);
                    } else if (item.type == "constraint") {
                        is_success = (item.current_val <= item.target_val);
                    }

                    std::string final_status = is_success ? "completed" : "failed";
                    
                    if (!is_success) {
                        for (const auto& [item_id, option] : rollovers) {
                            if (item_id == item.id) {
                                if (option == "push") {
                                    // Prepare tomorrow's item
                                    objective_record tomorrow_item;
                                    tomorrow_item.parent_id = item.parent_id;
                                    tomorrow_item.period = "daily";
                                    tomorrow_item.title = item.title;
                                    tomorrow_item.type = item.type;
                                    tomorrow_item.target_val = item.target_val;
                                    tomorrow_item.current_val = 0.0;
                                    tomorrow_item.status = "pending";
                                    new_rollover_items.push_back(tomorrow_item);
                                    
                                    final_status = "dropped";
                                } else {
                                    final_status = "dropped";
                                }
                                break;
                            }
                        }
                    }

                    item.status = final_status;
                    auto now = std::chrono::system_clock::now();
                    item.updated_at = std::format("{:%F %T}", now);
                }
            }

            // Add rollover items (need to assign unique IDs)
            if (!new_rollover_items.empty()) {
                auto tomorrow_ctx = get_tomorrow_date_context();
                int next_id = 1;
                for (const auto& o : objectives_) {
                    if (o.id >= next_id) next_id = o.id + 1;
                }
                
                auto now = std::chrono::system_clock::now();
                std::string now_str = std::format("{:%F %T}", now);
                
                for (auto& item : new_rollover_items) {
                    item.id = next_id++;
                    item.period_identifier = tomorrow_ctx.date;
                    item.created_at = now_str;
                    item.updated_at = now_str;
                    objectives_.push_back(item);
                }
            }

            // Mark ledger as closed
            bool found = false;
            auto now = std::chrono::system_clock::now();
            std::string now_str = std::format("{:%F %T}", now);
            for (auto& entry : ledger_) {
                if (entry.date == date) {
                    entry.closed = 1;
                    entry.closed_at = now_str;
                    found = true;
                    break;
                }
            }
            if (!found) {
                ledger_record entry;
                entry.date = date;
                entry.closed = 1;
                entry.closed_at = now_str;
                ledger_.push_back(entry);
            }

            save_data_unlocked();
        }

    private:
        std::filesystem::path dir_path_;
        std::vector<objective_record> objectives_;
        std::vector<ledger_record> ledger_;
        std::mutex mutex_;

        void delete_objective_recursive(int id) {
            std::vector<int> child_ids;
            for (const auto& o : objectives_) {
                if (o.parent_id && *(o.parent_id) == id) {
                    child_ids.push_back(o.id);
                }
            }
            for (int child_id : child_ids) {
                delete_objective_recursive(child_id);
            }
            objectives_.erase(
                std::remove_if(objectives_.begin(), objectives_.end(), [id](const auto& o) { return o.id == id; }),
                objectives_.end()
            );
        }

        void load_data() {
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                auto obj_path = dir_path_ / "objectives.json";
                if (std::filesystem::exists(obj_path)) {
                    std::string content;
                    std::ifstream f(obj_path);
                    if (f) {
                        content.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                        auto err = glz::read_json(objectives_, content);
                        if (err) {
                            // Empty or parse error, fallback
                        }
                    }
                }
            } catch (...) {}

            try {
                auto ledger_path = dir_path_ / "ledger.json";
                if (std::filesystem::exists(ledger_path)) {
                    std::string content;
                    std::ifstream f(ledger_path);
                    if (f) {
                        content.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                        auto err = glz::read_json(ledger_, content);
                        if (err) {
                            // Empty or parse error, fallback
                        }
                    }
                }
            } catch (...) {}
        }

        void save_data_unlocked() {
            try {
                auto obj_path = dir_path_ / "objectives.json";
                std::string content = glz::write_json(objectives_).value_or("");
                if (!content.empty()) {
                    std::ofstream f(obj_path);
                    if (f) {
                        f << content;
                    }
                }
            } catch (...) {}

            try {
                auto ledger_path = dir_path_ / "ledger.json";
                std::string content = glz::write_json(ledger_).value_or("");
                if (!content.empty()) {
                    std::ofstream f(ledger_path);
                    if (f) {
                        f << content;
                    }
                }
            } catch (...) {}
        }
    };
}
