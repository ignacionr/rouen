#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <chrono>
#include <algorithm>

namespace rouen::helpers {

struct CardMetricEntry {
    std::string title;
    std::string uri;
    double last_render_ms{0.0};
    double avg_render_ms{0.0};
    double max_render_ms{0.0};
    double min_render_ms{0.0};
    uint64_t render_count{0};
    uint64_t slow_render_count{0};
    uint64_t very_slow_render_count{0};
    std::chrono::steady_clock::time_point last_updated{};
};

class CardRenderMetrics {
public:
    static CardRenderMetrics& instance() {
        static CardRenderMetrics inst;
        return inst;
    }

    void record(const std::string& title, const std::string& uri, double render_ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = uri.empty() ? title : uri;
        if (key.empty()) {
            key = "Unknown Card";
        }
        auto& entry = metrics_[key];
        entry.title = title.empty() ? key : title;
        entry.uri = uri;
        entry.last_render_ms = render_ms;
        if (render_ms > entry.max_render_ms) {
            entry.max_render_ms = render_ms;
        }
        if (entry.render_count == 0 || render_ms < entry.min_render_ms) {
            entry.min_render_ms = render_ms;
        }
        if (render_ms >= 100.0) {
            entry.slow_render_count++;
        }
        if (render_ms >= 500.0) {
            entry.very_slow_render_count++;
        }
        if (entry.render_count == 0) {
            entry.avg_render_ms = render_ms;
        } else {
            // Exponential moving average (alpha = 0.1)
            entry.avg_render_ms = entry.avg_render_ms * 0.9 + render_ms * 0.1;
        }
        entry.render_count++;
        entry.last_updated = std::chrono::steady_clock::now();
    }

    std::vector<CardMetricEntry> get_all_metrics(bool include_inactive = false) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<CardMetricEntry> result;
        result.reserve(metrics_.size());
        auto now = std::chrono::steady_clock::now();
        for (const auto& [key, entry] : metrics_) {
            // Include active (seen in last 5s) or all if requested
            if (include_inactive || std::chrono::duration_cast<std::chrono::seconds>(now - entry.last_updated).count() < 5) {
                result.push_back(entry);
            }
        }
        // Sort by average render time descending (slowest cards at the top)
        std::sort(result.begin(), result.end(), [](const CardMetricEntry& a, const CardMetricEntry& b) {
            return a.avg_render_ms > b.avg_render_ms;
        });
        return result;
    }

    std::optional<CardMetricEntry> get_metric_for_key(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = metrics_.find(key);
        if (it != metrics_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, CardMetricEntry> metrics_;
};

} // namespace rouen::helpers
