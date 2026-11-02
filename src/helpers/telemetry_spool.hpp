#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <string>
#include <vector>

#include "../models/series/series_repository.hpp"
#include "adaptive_cards/parser.hpp"
#include "glaze_include.hpp"
#include "platform_utils.hpp"
#include "sqlite.hpp"

namespace rouen::helpers {

struct telemetry_sample {
    long long seq_id{0};
    std::string source_uri;
    std::string series_name;
    long long timestamp_ms{0};
    double value{0.0};
    int synced{0};
};

class telemetry_spool {
public:
    static telemetry_spool& instance() {
        static telemetry_spool inst;
        return inst;
    }

    telemetry_spool() : db_(resolve_db_path()) {
        ensure_schema();
    }

    explicit telemetry_spool(const std::string& custom_db_path) : db_(custom_db_path) {
        ensure_schema();
    }

    void set_max_rows(std::size_t max_rows) {
        max_rows_ = std::max<std::size_t>(1, max_rows);
        enforce_ring_buffer_capacity();
    }

    [[nodiscard]] std::size_t max_rows() const {
        return max_rows_;
    }

    void enqueue_sample(
        const std::string& source_uri,
        const std::string& series_name,
        long long timestamp_ms,
        double value) {
        if (series_name.empty()) {
            return;
        }
        long long const ts = timestamp_ms > 0
            ? timestamp_ms
            : std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();

        db_.exec(
            "INSERT INTO telemetry_spool (source_uri, series_name, timestamp_ms, value, synced) "
            "VALUES (?, ?, ?, ?, 0);",
            nullptr,
            source_uri,
            series_name,
            ts,
            value);
        enforce_ring_buffer_capacity();
    }

    void enqueue_envelope(
        const std::string& source_uri,
        const adaptive_cards::telemetry_envelope& env) {
        if (env.series.empty()) {
            return;
        }
        long long const ts = env.timestamp_ms > 0
            ? env.timestamp_ms
            : std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();

        std::vector<telemetry_sample> local_samples;
        local_samples.reserve(env.series.size());
        for (const auto& [name, val] : env.series) {
            enqueue_sample(source_uri, name, ts, val);
            local_samples.push_back(telemetry_sample{
                .seq_id = 0,
                .source_uri = source_uri,
                .series_name = name,
                .timestamp_ms = ts,
                .value = val,
                .synced = 0
            });
        }
        ingest_backfill_batch(local_samples);
    }

    [[nodiscard]] std::size_t pending_unsynced_count() {
        long long count = 0;
        db_.exec(
            "SELECT COUNT(*) FROM telemetry_spool WHERE synced = 0;",
            [&count](sqlite3_stmt* stmt) {
                count = sqlite3_column_int64(stmt, 0);
            });
        return static_cast<std::size_t>(std::max<long long>(0, count));
    }

    [[nodiscard]] std::vector<telemetry_sample> drain_unsynced_batch(std::size_t limit = 500) {
        std::vector<telemetry_sample> out;
        auto const safe_limit = static_cast<long long>(limit == 0 ? 500 : limit);
        db_.exec(
            "SELECT seq_id, source_uri, series_name, timestamp_ms, value, synced "
            "FROM telemetry_spool WHERE synced = 0 ORDER BY seq_id ASC LIMIT ?;",
            [&out](sqlite3_stmt* stmt) {
                auto col_str = [stmt](int idx) -> std::string {
                    const unsigned char* txt = sqlite3_column_text(stmt, idx);
                    return txt ? reinterpret_cast<const char*>(txt) : "";
                };
                telemetry_sample s{};
                s.seq_id = sqlite3_column_int64(stmt, 0);
                s.source_uri = col_str(1);
                s.series_name = col_str(2);
                s.timestamp_ms = sqlite3_column_int64(stmt, 3);
                s.value = sqlite3_column_double(stmt, 4);
                s.synced = sqlite3_column_int(stmt, 5);
                out.push_back(std::move(s));
            },
            safe_limit);
        return out;
    }

    void acknowledge_up_to(long long max_seq_id) {
        db_.exec(
            "UPDATE telemetry_spool SET synced = 1 WHERE seq_id <= ?;",
            nullptr,
            max_seq_id);
    }

    // Merges backfilled historical telemetry samples into series_repository without duplicate timestamps
    void ingest_backfill_batch(const std::vector<telemetry_sample>& batch) {
        if (batch.empty()) {
            return;
        }
        std::map<std::string, std::map<long long, float>> grouped;
        for (const auto& s : batch) {
            if (!s.series_name.empty()) {
                grouped[s.series_name][s.timestamp_ms] = static_cast<float>(s.value);
            }
        }

        models::series::series_repository repo{};
        for (const auto& [series_name, new_pts] : grouped) {
            models::series::series_record rec{};
            std::map<std::string, float> merged_by_label;
            if (auto existing = repo.get_series_by_name(series_name); existing.has_value()) {
                rec = *existing;
                for (const auto& pt : existing->points) {
                    merged_by_label[pt.label] = pt.value;
                }
            } else {
                rec.name = series_name;
                rec.title = series_name;
            }

            for (const auto& [ts_ms, val] : new_pts) {
                merged_by_label[std::to_string(ts_ms)] = val;
            }

            rec.points.clear();
            rec.points.reserve(merged_by_label.size());
            for (const auto& [lbl, val] : merged_by_label) {
                rec.points.push_back(models::series::data_point{
                    .label = lbl,
                    .value = val
                });
            }
            (void)repo.save_series(rec);
        }
    }

    void clear_all() {
        db_.exec("DELETE FROM telemetry_spool;");
    }

private:
    static std::string resolve_db_path() {
        auto const path = platform::get_user_data_path("telemetry_spool.db");
        std::filesystem::create_directories(path.parent_path());
        return path.string();
    }

    void ensure_schema() {
        db_.ensure_table(
            "telemetry_spool",
            "seq_id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "source_uri TEXT NOT NULL, "
            "series_name TEXT NOT NULL, "
            "timestamp_ms INTEGER NOT NULL, "
            "value REAL NOT NULL, "
            "synced INTEGER NOT NULL DEFAULT 0");
    }

    void enforce_ring_buffer_capacity() {
        long long total = 0;
        db_.exec(
            "SELECT COUNT(*) FROM telemetry_spool;",
            [&total](sqlite3_stmt* stmt) {
                total = sqlite3_column_int64(stmt, 0);
            });
        long long const cap = static_cast<long long>(max_rows_);
        if (total > cap) {
            long long const excess = total - cap;
            db_.exec(
                "DELETE FROM telemetry_spool WHERE seq_id IN ("
                "SELECT seq_id FROM telemetry_spool ORDER BY seq_id ASC LIMIT ?"
                ");",
                nullptr,
                excess);
        }
    }

    hosting::db::sqlite db_;
    std::size_t max_rows_{100000};
};

} // namespace rouen::helpers

template <>
struct glz::meta<rouen::helpers::telemetry_sample> {
    using T = rouen::helpers::telemetry_sample;
    static constexpr auto value = object(
        "seq_id", &T::seq_id,
        "source_uri", &T::source_uri,
        "series_name", &T::series_name,
        "timestamp_ms", &T::timestamp_ms,
        "value", &T::value,
        "synced", &T::synced
    );
};
