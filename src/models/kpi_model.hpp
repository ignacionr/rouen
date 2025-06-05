#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <memory>
#include <future>
#include <mutex>
#include <thread>
#include <functional>

#include "../helpers/glaze_include.hpp"
#include "../helpers/debug.hpp"

// KPI-specific logging macros
#define KPI_ERROR(message) LOG_COMPONENT("KPI", LOG_LEVEL_ERROR, message)
#define KPI_WARN(message) LOG_COMPONENT("KPI", LOG_LEVEL_WARN, message)
#define KPI_INFO(message) LOG_COMPONENT("KPI", LOG_LEVEL_INFO, message)
#define KPI_DEBUG(message) LOG_COMPONENT("KPI", LOG_LEVEL_DEBUG, message)
#define KPI_TRACE(message) LOG_COMPONENT("KPI", LOG_LEVEL_TRACE, message)

// Format-enabled macros
#define KPI_ERROR_FMT(fmt, ...) KPI_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define KPI_WARN_FMT(fmt, ...) KPI_WARN(debug::format_log(fmt, __VA_ARGS__))
#define KPI_INFO_FMT(fmt, ...) KPI_INFO(debug::format_log(fmt, __VA_ARGS__))
#define KPI_DEBUG_FMT(fmt, ...) KPI_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define KPI_TRACE_FMT(fmt, ...) KPI_TRACE(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::models {

// KPI value types
enum class kpi_value_type {
    number,         // Numeric value (int, double)
    percentage,     // Percentage value (0-100)
    currency,       // Currency value
    count,          // Count/quantity
    ratio,          // Ratio (e.g., 3:1)
    boolean,        // Yes/No, True/False
    text            // Text/string value
};

// KPI trend direction
enum class kpi_trend {
    up,
    down,
    stable,
    unknown,
    IMPROVING,
    STABLE,
    DECLINING,
    CRITICAL
};

// KPI target comparison
enum class kpi_target_status {
    above_target,   // Current value is above target (good for growth metrics)
    on_target,      // Current value meets target
    below_target,   // Current value is below target
    no_target       // No target set
};

// KPI target type
enum class kpi_target_type {
    MINIMUM,        // Target is a minimum value to achieve
    MAXIMUM,        // Target is a maximum value not to exceed
    EXACT           // Target is an exact value to match
};

// Aggregation types for hierarchical KPIs
enum class kpi_aggregation {
    sum,            // Sum all child values
    average,        // Average of child values
    weighted_avg,   // Weighted average
    min,            // Minimum value
    max,            // Maximum value
    count,          // Count of child KPIs
    custom          // Custom aggregation logic
};

// Time period for KPI reporting
enum class kpi_period {
    daily,
    weekly,
    monthly,
    quarterly,
    yearly,
    custom,
    DAILY,
    WEEKLY,
    MONTHLY,
    QUARTERLY,
    YEARLY,
    CUSTOM
};

// KPI data point (historical value)
struct kpi_data_point {
    std::string timestamp;
    double value;
    std::string note;
    std::string source;  // Where this data came from
    
    struct glaze {
        using T = kpi_data_point;
        static constexpr auto value = glz::object(
            "timestamp", &T::timestamp,
            "value", &T::value,
            "note", &T::note,
            "source", &T::source
        );
    };
};

// KPI target definition
struct kpi_target {
    double value;
    double target_value;        // Target value (for compatibility with dashboard code)
    kpi_target_type target_type;
    std::string period;         // "monthly", "quarterly", etc.
    std::string description;
    bool is_upper_bound;        // true if target is a maximum, false for minimum
    
    struct glaze {
        using T = kpi_target;
        static constexpr auto value = glz::object(
            "value", &T::value,
            "target_value", &T::target_value,
            "target_type", &T::target_type,
            "period", &T::period,
            "description", &T::description,
            "is_upper_bound", &T::is_upper_bound
        );
    };
};

// KPI category for organization
struct kpi_category {
    std::string id;
    std::string name;
    std::string description;
    std::string color;          // Hex color for UI
    std::string icon;           // Icon identifier
    int sort_order;
    
    struct glaze {
        using T = kpi_category;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "name", &T::name,
            "description", &T::description,
            "color", &T::color,
            "icon", &T::icon,
            "sort_order", &T::sort_order
        );
    };
};

// Main KPI structure
struct kpi {
    std::string id;
    std::string name;
    std::string description;
    kpi_value_type value_type;
    std::string category_id;
    std::string parent_id;      // For hierarchical KPIs
    
    // Current value and metadata
    double current_value;
    std::string unit;           // "USD", "%", "units", etc.
    std::string last_updated;
    
    // Historical data
    std::vector<kpi_data_point> history;
    
    // Targets and goals
    std::optional<kpi_target> target;
    
    // Hierarchy and aggregation
    kpi_aggregation aggregation_type;
    std::vector<std::string> child_ids;
    std::map<std::string, double> weights; // For weighted aggregations
    
    // Metadata
    std::string owner;          // Person responsible
    kpi_period reporting_period;
    bool is_active;
    int sort_order;
    std::map<std::string, std::string> tags;
    
    // Calculated fields (not persisted)
    kpi_trend trend;
    kpi_target_status target_status;
    double trend_percentage;    // % change from previous period
    
    struct glaze {
        using T = kpi;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "name", &T::name,
            "description", &T::description,
            "value_type", &T::value_type,
            "category_id", &T::category_id,
            "parent_id", &T::parent_id,
            "current_value", &T::current_value,
            "unit", &T::unit,
            "last_updated", &T::last_updated,
            "history", &T::history,
            "target", &T::target,
            "aggregation_type", &T::aggregation_type,
            "child_ids", &T::child_ids,
            "weights", &T::weights,
            "owner", &T::owner,
            "reporting_period", &T::reporting_period,
            "is_active", &T::is_active,
            "sort_order", &T::sort_order,
            "tags", &T::tags
        );
    };
};

// KPI dashboard configuration
struct kpi_dashboard {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> kpi_ids;
    std::string layout;         // JSON layout configuration
    std::string owner;
    bool is_default;
    std::string created_at;
    std::string updated_at;
    
    struct glaze {
        using T = kpi_dashboard;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "name", &T::name,
            "description", &T::description,
            "kpi_ids", &T::kpi_ids,
            "layout", &T::layout,
            "owner", &T::owner,
            "is_default", &T::is_default,
            "created_at", &T::created_at,
            "updated_at", &T::updated_at
        );
    };
};

// KPI report configuration
struct kpi_report {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> kpi_ids;
    std::vector<std::string> category_ids;
    kpi_period period;
    std::string start_date;
    std::string end_date;
    std::string template_config;    // JSON configuration for report template
    bool auto_generate;
    std::string schedule;           // Cron-like schedule for auto-generation
    
    struct glaze {
        using T = kpi_report;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "name", &T::name,
            "description", &T::description,
            "kpi_ids", &T::kpi_ids,
            "category_ids", &T::category_ids,
            "period", &T::period,
            "start_date", &T::start_date,
            "end_date", &T::end_date,
            "template_config", &T::template_config,
            "auto_generate", &T::auto_generate,
            "schedule", &T::schedule
        );
    };
};

// Forward declaration of KPI repository for data persistence
class kpi_repository;

// Main KPI model class
class kpi_model {
public:
    kpi_model();
    ~kpi_model();
    
    // Category management
    std::future<std::vector<kpi_category>> get_categories();
    std::future<bool> create_category(const kpi_category& category);
    std::future<bool> update_category(const kpi_category& category);
    std::future<bool> delete_category(const std::string& category_id);
    
    // KPI management
    std::future<std::vector<kpi>> get_kpis(const std::string& category_id = "");
    std::future<std::vector<kpi>> get_kpis_by_category(const std::string& category_id);
    std::future<std::optional<kpi>> get_kpi(const std::string& kpi_id);
    std::future<std::vector<kpi>> get_kpi_hierarchy(const std::string& parent_id = "");
    std::future<bool> create_kpi(const kpi& new_kpi);
    std::future<bool> update_kpi(const kpi& updated_kpi);
    std::future<bool> delete_kpi(const std::string& kpi_id);
    
    // KPI data management
    std::future<bool> update_kpi_value(const std::string& kpi_id, double value, const std::string& note = "");
    std::future<bool> add_kpi_data_point(const std::string& kpi_id, const kpi_data_point& data_point);
    std::future<std::vector<kpi_data_point>> get_kpi_history(const std::string& kpi_id, const std::string& start_date = "", const std::string& end_date = "");
    
    // Aggregation and calculations
    std::future<bool> recalculate_hierarchy();
    std::future<bool> recalculate_kpi(const std::string& kpi_id);
    std::future<std::map<std::string, double>> calculate_aggregations(const std::string& parent_id);
    
    // Dashboard management
    std::future<std::vector<kpi_dashboard>> get_dashboards();
    std::future<std::optional<kpi_dashboard>> get_dashboard(const std::string& dashboard_id);
    std::future<bool> create_dashboard(const kpi_dashboard& dashboard);
    std::future<bool> update_dashboard(const kpi_dashboard& dashboard);
    std::future<bool> delete_dashboard(const std::string& dashboard_id);
    
    // Report management
    std::future<std::vector<kpi_report>> get_reports();
    std::future<std::optional<kpi_report>> get_report(const std::string& report_id);
    std::future<bool> create_report(const kpi_report& report);
    std::future<bool> update_report(const kpi_report& report);
    std::future<bool> delete_report(const std::string& report_id);
    std::future<std::string> generate_report(const std::string& report_id);
    
    // Analytics and insights
    std::future<std::map<std::string, kpi_trend>> calculate_trends(const std::vector<std::string>& kpi_ids, int days = 30);
    std::future<std::map<std::string, kpi_target_status>> check_targets(const std::vector<std::string>& kpi_ids);
    std::future<std::vector<kpi>> get_kpis_missing_targets();
    std::future<std::vector<kpi>> get_kpis_needing_update(int days_threshold = 7);
    
    // Import/Export
    std::future<bool> import_kpis(const std::string& json_data);
    std::future<std::string> export_kpis(const std::vector<std::string>& kpi_ids = {});
    std::future<bool> import_from_csv(const std::string& csv_data, const std::string& kpi_id);
    
    // Integration helpers
    std::future<bool> sync_with_jira(const std::string& kpi_id, const std::string& jql_query);
    std::future<bool> sync_with_external_api(const std::string& kpi_id, const std::string& api_endpoint, const std::string& value_path);
    
    // Utility methods
    static std::string generate_id();
    static std::string format_value(double value, kpi_value_type type, const std::string& unit);
    static std::string get_current_timestamp();
    static kpi_trend calculate_trend(const std::vector<kpi_data_point>& history, int periods = 3);
    static double calculate_trend_percentage(const std::vector<kpi_data_point>& history, int periods = 2);
    
    // Async execution helper template
    template<typename T>
    static void execute_async(
        std::future<T>&& future,
        std::function<void(const T&)> on_success,
        std::function<void(const std::string&)> on_error = nullptr
    ) {
        std::thread([future = std::move(future), on_success, on_error]() mutable {
            try {
                auto result = future.get();
                if (on_success) {
                    on_success(result);
                }
            } catch (const std::exception& e) {
                if (on_error) {
                    on_error(e.what());
                }
            }
        }).detach();
    }
    
    // Specialization for bool return type (for create/update/delete operations)
    static void execute_async_bool(
        std::future<bool>&& future,
        std::function<void(bool)> on_success,
        std::function<void(const std::string&)> on_error = nullptr
    ) {
        std::thread([future = std::move(future), on_success, on_error]() mutable {
            try {
                auto result = future.get();
                if (on_success) {
                    on_success(result);
                }
            } catch (const std::exception& e) {
                if (on_error) {
                    on_error(e.what());
                }
            }
        }).detach();
    }

private:
    std::unique_ptr<kpi_repository> repository_;
    mutable std::mutex data_mutex_;
    
    // Helper methods
    void calculate_derived_fields(kpi& target_kpi);
    bool validate_kpi(const kpi& target_kpi);
    void update_parent_aggregations(const std::string& kpi_id);
    double apply_aggregation(kpi_aggregation type, const std::vector<double>& values, const std::map<std::string, double>& weights = {});
};

} // namespace rouen::models
