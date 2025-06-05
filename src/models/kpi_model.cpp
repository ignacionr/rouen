#include "kpi_model.hpp"
#include "kpi_repository.hpp"
#include "../helpers/glaze_include.hpp"
#include "../helpers/config_service.hpp"
#include <format>
#include <chrono>
#include <algorithm>
#include <thread>
#include <random>
#include <sstream>
#include <iomanip>

namespace rouen::models {

kpi_model::kpi_model() {
    KPI_INFO("Initializing KPI model");
    
    try {
        // Get database path from config service
        auto config = rouen::helpers::ConfigService::instance();
        std::string db_path = config->get_executable_directory() + "/kpi.db";
        
        repository_ = std::make_unique<kpi_repository>(db_path);
        
        KPI_INFO("KPI model initialized successfully");
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Failed to initialize KPI model: {}", e.what());
        throw;
    }
}

kpi_model::~kpi_model() {
    KPI_INFO("KPI model destructor called");
}

// Category management
std::future<std::vector<kpi_category>> kpi_model::get_categories() {
    return std::async(std::launch::async, [this]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->get_categories();
    });
}

std::future<bool> kpi_model::create_category(const kpi_category& category) {
    return std::async(std::launch::async, [this, category]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->create_category(category);
    });
}

std::future<bool> kpi_model::update_category(const kpi_category& category) {
    return std::async(std::launch::async, [this, category]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->update_category(category);
    });
}

std::future<bool> kpi_model::delete_category(const std::string& category_id) {
    return std::async(std::launch::async, [this, category_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->delete_category(category_id);
    });
}

// KPI management
std::future<std::vector<kpi>> kpi_model::get_kpis(const std::string& category_id) {
    return std::async(std::launch::async, [this, category_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        auto kpis = repository_->get_kpis(category_id);
        
        // Calculate derived fields for each KPI
        for (auto& k : kpis) {
            calculate_derived_fields(k);
        }
        
        return kpis;
    });
}

std::future<std::vector<kpi>> kpi_model::get_kpis_by_category(const std::string& category_id) {
    return std::async(std::launch::async, [this, category_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        auto kpis = repository_->get_kpis(category_id);
        
        // Calculate derived fields for each KPI
        for (auto& k : kpis) {
            calculate_derived_fields(k);
        }
        
        return kpis;
    });
}

std::future<std::optional<kpi>> kpi_model::get_kpi(const std::string& kpi_id) {
    return std::async(std::launch::async, [this, kpi_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        auto kpi_opt = repository_->get_kpi(kpi_id);
        
        if (kpi_opt) {
            calculate_derived_fields(*kpi_opt);
        }
        
        return kpi_opt;
    });
}

std::future<std::vector<kpi>> kpi_model::get_kpi_hierarchy(const std::string& parent_id) {
    return std::async(std::launch::async, [this, parent_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        auto kpis = repository_->get_kpi_hierarchy(parent_id);
        
        // Calculate derived fields for each KPI
        for (auto& k : kpis) {
            calculate_derived_fields(k);
        }
        
        return kpis;
    });
}

std::future<bool> kpi_model::create_kpi(const kpi& new_kpi) {
    return std::async(std::launch::async, [this, new_kpi]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        // Validate KPI before creating
        kpi validated_kpi = new_kpi;
        if (!validate_kpi(validated_kpi)) {
            KPI_ERROR_FMT("KPI validation failed for: {}", new_kpi.name);
            return false;
        }
        
        // Set timestamps
        validated_kpi.last_updated = get_current_timestamp();
        
        bool result = repository_->create_kpi(validated_kpi);
        
        if (result && !validated_kpi.parent_id.empty()) {
            update_parent_aggregations(validated_kpi.parent_id);
        }
        
        return result;
    });
}

std::future<bool> kpi_model::update_kpi(const kpi& updated_kpi) {
    return std::async(std::launch::async, [this, updated_kpi]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        // Validate KPI before updating
        kpi validated_kpi = updated_kpi;
        if (!validate_kpi(validated_kpi)) {
            KPI_ERROR_FMT("KPI validation failed for: {}", updated_kpi.name);
            return false;
        }
        
        bool result = repository_->update_kpi(validated_kpi);
        
        if (result && !validated_kpi.parent_id.empty()) {
            update_parent_aggregations(validated_kpi.parent_id);
        }
        
        return result;
    });
}

std::future<bool> kpi_model::delete_kpi(const std::string& kpi_id) {
    return std::async(std::launch::async, [this, kpi_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        // Get the KPI to find its parent
        auto kpi_opt = repository_->get_kpi(kpi_id);
        std::string parent_id;
        if (kpi_opt) {
            parent_id = kpi_opt->parent_id;
        }
        
        bool result = repository_->delete_kpi(kpi_id);
        
        if (result && !parent_id.empty()) {
            update_parent_aggregations(parent_id);
        }
        
        return result;
    });
}

// KPI data management
std::future<bool> kpi_model::update_kpi_value(const std::string& kpi_id, double value, const std::string& note) {
    return std::async(std::launch::async, [this, kpi_id, value, note]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        bool result = repository_->update_kpi_value(kpi_id, value, note);
        
        if (result) {
            // Update parent aggregations
            auto kpi_opt = repository_->get_kpi(kpi_id);
            if (kpi_opt && !kpi_opt->parent_id.empty()) {
                update_parent_aggregations(kpi_opt->parent_id);
            }
        }
        
        return result;
    });
}

std::future<bool> kpi_model::add_kpi_data_point(const std::string& kpi_id, const kpi_data_point& data_point) {
    return std::async(std::launch::async, [this, kpi_id, data_point]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->add_kpi_data_point(kpi_id, data_point);
    });
}

std::future<std::vector<kpi_data_point>> kpi_model::get_kpi_history(const std::string& kpi_id, const std::string& start_date, const std::string& end_date) {
    return std::async(std::launch::async, [this, kpi_id, start_date, end_date]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->get_kpi_history(kpi_id, start_date, end_date);
    });
}

// Aggregation and calculations
std::future<bool> kpi_model::recalculate_hierarchy() {
    return std::async(std::launch::async, [this]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        try {
            // Get all KPIs with parent relationships
            auto all_kpis = repository_->get_kpis();
            
            // Find root KPIs (those without parents) and process hierarchy
            for (const auto& k : all_kpis) {
                if (k.parent_id.empty()) {
                    recalculate_kpi(k.id).wait();
                }
            }
            
            KPI_INFO("KPI hierarchy recalculation completed");
            return true;
        } catch (const std::exception& e) {
            KPI_ERROR_FMT("Error recalculating hierarchy: {}", e.what());
            return false;
        }
    });
}

std::future<bool> kpi_model::recalculate_kpi(const std::string& kpi_id) {
    return std::async(std::launch::async, [this, kpi_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        try {
            auto kpi_opt = repository_->get_kpi(kpi_id);
            if (!kpi_opt) {
                KPI_WARN_FMT("KPI not found for recalculation: {}", kpi_id);
                return false;
            }
            
            auto& k = *kpi_opt;
            
            // If this KPI has children, recalculate based on aggregation
            if (!k.child_ids.empty()) {
                auto aggregations = calculate_aggregations(kpi_id).get();
                if (aggregations.find("value") != aggregations.end()) {
                    k.current_value = aggregations["value"];
                    k.last_updated = get_current_timestamp();
                    repository_->update_kpi(k);
                }
            }
            
            // Recalculate derived fields
            calculate_derived_fields(k);
            
            // Recursively recalculate children
            for (const auto& child_id : k.child_ids) {
                recalculate_kpi(child_id).wait();
            }
            
            return true;
        } catch (const std::exception& e) {
            KPI_ERROR_FMT("Error recalculating KPI {}: {}", kpi_id, e.what());
            return false;
        }
    });
}

std::future<std::map<std::string, double>> kpi_model::calculate_aggregations(const std::string& parent_id) {
    return std::async(std::launch::async, [this, parent_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        std::map<std::string, double> results;
        
        try {
            auto parent_opt = repository_->get_kpi(parent_id);
            if (!parent_opt) {
                return results;
            }
            
            const auto& parent = *parent_opt;
            std::vector<double> child_values;
            
            // Collect values from child KPIs
            for (const auto& child_id : parent.child_ids) {
                auto child_opt = repository_->get_kpi(child_id);
                if (child_opt && child_opt->is_active) {
                    child_values.push_back(child_opt->current_value);
                }
            }
            
            if (!child_values.empty()) {
                results["value"] = apply_aggregation(parent.aggregation_type, child_values, parent.weights);
                results["count"] = static_cast<double>(child_values.size());
                
                if (!child_values.empty()) {
                    results["min"] = *std::min_element(child_values.begin(), child_values.end());
                    results["max"] = *std::max_element(child_values.begin(), child_values.end());
                }
            }
        } catch (const std::exception& e) {
            KPI_ERROR_FMT("Error calculating aggregations for {}: {}", parent_id, e.what());
        }
        
        return results;
    });
}

// Dashboard management
std::future<std::vector<kpi_dashboard>> kpi_model::get_dashboards() {
    return std::async(std::launch::async, [this]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->get_dashboards();
    });
}

std::future<std::optional<kpi_dashboard>> kpi_model::get_dashboard(const std::string& dashboard_id) {
    return std::async(std::launch::async, [this, dashboard_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->get_dashboard(dashboard_id);
    });
}

std::future<bool> kpi_model::create_dashboard(const kpi_dashboard& dashboard) {
    return std::async(std::launch::async, [this, dashboard]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->create_dashboard(dashboard);
    });
}

std::future<bool> kpi_model::update_dashboard(const kpi_dashboard& dashboard) {
    return std::async(std::launch::async, [this, dashboard]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->update_dashboard(dashboard);
    });
}

std::future<bool> kpi_model::delete_dashboard(const std::string& dashboard_id) {
    return std::async(std::launch::async, [this, dashboard_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->delete_dashboard(dashboard_id);
    });
}

// Report management
std::future<std::vector<kpi_report>> kpi_model::get_reports() {
    return std::async(std::launch::async, [this]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->get_reports();
    });
}

std::future<std::optional<kpi_report>> kpi_model::get_report(const std::string& report_id) {
    return std::async(std::launch::async, [this, report_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->get_report(report_id);
    });
}

std::future<bool> kpi_model::create_report(const kpi_report& report) {
    return std::async(std::launch::async, [this, report]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->create_report(report);
    });
}

std::future<bool> kpi_model::update_report(const kpi_report& report) {
    return std::async(std::launch::async, [this, report]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->update_report(report);
    });
}

std::future<bool> kpi_model::delete_report(const std::string& report_id) {
    return std::async(std::launch::async, [this, report_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->delete_report(report_id);
    });
}

std::future<std::string> kpi_model::generate_report(const std::string& report_id) {
    return std::async(std::launch::async, [this, report_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        try {
            auto report_opt = repository_->get_report(report_id);
            if (!report_opt) {
                return std::string("Report not found");
            }
            
            const auto& report = *report_opt;
            std::ostringstream report_content;
            
            // Generate report header
            report_content << "# KPI Report: " << report.name << "\n\n";
            report_content << "**Description:** " << report.description << "\n";
            report_content << "**Period:** " << static_cast<int>(report.period) << "\n";
            report_content << "**Date Range:** " << report.start_date << " - " << report.end_date << "\n";
            report_content << "**Generated:** " << get_current_timestamp() << "\n\n";
            
            // Add KPI data
            for (const auto& kpi_id : report.kpi_ids) {
                auto kpi_opt = repository_->get_kpi(kpi_id);
                if (kpi_opt) {
                    const auto& k = *kpi_opt;
                    report_content << "## " << k.name << "\n";
                    report_content << "**Current Value:** " << format_value(k.current_value, k.value_type, k.unit) << "\n";
                    report_content << "**Last Updated:** " << k.last_updated << "\n";
                    
                    if (k.target) {
                        report_content << "**Target:** " << format_value(k.target->value, k.value_type, k.unit) << "\n";
                    }
                    
                    report_content << "\n";
                }
            }
            
            return report_content.str();
        } catch (const std::exception& e) {
            KPI_ERROR_FMT("Error generating report: {}", e.what());
            return std::string("Error generating report");
        }
    });
}

// Analytics and insights
std::future<std::map<std::string, kpi_trend>> kpi_model::calculate_trends(const std::vector<std::string>& kpi_ids, int days) {
    return std::async(std::launch::async, [this, kpi_ids, days]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        std::map<std::string, kpi_trend> trends;
        
        for (const auto& kpi_id : kpi_ids) {
            auto kpi_opt = repository_->get_kpi(kpi_id);
            if (kpi_opt) {
                trends[kpi_id] = calculate_trend(kpi_opt->history, days);
            }
        }
        
        return trends;
    });
}

std::future<std::map<std::string, kpi_target_status>> kpi_model::check_targets(const std::vector<std::string>& kpi_ids) {
    return std::async(std::launch::async, [this, kpi_ids]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        std::map<std::string, kpi_target_status> statuses;
        
        for (const auto& kpi_id : kpi_ids) {
            auto kpi_opt = repository_->get_kpi(kpi_id);
            if (kpi_opt) {
                const auto& k = *kpi_opt;
                if (k.target) {
                    if (k.target->is_upper_bound) {
                        if (k.current_value <= k.target->value) {
                            statuses[kpi_id] = kpi_target_status::on_target;
                        } else {
                            statuses[kpi_id] = kpi_target_status::above_target;
                        }
                    } else {
                        if (k.current_value >= k.target->value) {
                            statuses[kpi_id] = kpi_target_status::on_target;
                        } else {
                            statuses[kpi_id] = kpi_target_status::below_target;
                        }
                    }
                } else {
                    statuses[kpi_id] = kpi_target_status::no_target;
                }
            }
        }
        
        return statuses;
    });
}

std::future<std::vector<kpi>> kpi_model::get_kpis_missing_targets() {
    return std::async(std::launch::async, [this]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->get_kpis_missing_targets();
    });
}

std::future<std::vector<kpi>> kpi_model::get_kpis_needing_update(int days_threshold) {
    return std::async(std::launch::async, [this, days_threshold]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return repository_->get_kpis_needing_update(days_threshold);
    });
}

// Import/Export
std::future<bool> kpi_model::import_kpis(const std::string& json_data) {
    return std::async(std::launch::async, [this, json_data]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        try {
            std::vector<kpi> kpis;
            auto result = glz::read_json(kpis, json_data);
            if (result) {
                KPI_ERROR("Failed to parse KPI JSON data");
                return false;
            }
            
            int imported = 0;
            for (const auto& k : kpis) {
                if (repository_->create_kpi(k)) {
                    imported++;
                }
            }
            
            KPI_INFO_FMT("Imported {} out of {} KPIs", imported, kpis.size());
            return imported > 0;
        } catch (const std::exception& e) {
            KPI_ERROR_FMT("Error importing KPIs: {}", e.what());
            return false;
        }
    });
}

std::future<std::string> kpi_model::export_kpis(const std::vector<std::string>& kpi_ids) {
    return std::async(std::launch::async, [this, kpi_ids]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        try {
            std::vector<kpi> kpis_to_export;
            
            if (kpi_ids.empty()) {
                // Export all KPIs
                kpis_to_export = repository_->get_kpis();
            } else {
                // Export specific KPIs
                for (const auto& kpi_id : kpi_ids) {
                    auto kpi_opt = repository_->get_kpi(kpi_id);
                    if (kpi_opt) {
                        kpis_to_export.push_back(*kpi_opt);
                    }
                }
            }
            
            std::string json_result;
            auto result = glz::write_json(kpis_to_export, json_result);
            if (result) {
                KPI_ERROR("Failed to serialize KPIs to JSON");
                return std::string("{}");
            }
            
            KPI_INFO_FMT("Exported {} KPIs", kpis_to_export.size());
            return json_result;
        } catch (const std::exception& e) {
            KPI_ERROR_FMT("Error exporting KPIs: {}", e.what());
            return std::string("{}");
        }
    });
}

std::future<bool> kpi_model::import_from_csv(const std::string& csv_data, const std::string& kpi_id) {
    return std::async(std::launch::async, [this, csv_data, kpi_id]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        try {
            std::istringstream stream(csv_data);
            std::string line;
            int imported = 0;
            
            // Skip header line if present
            if (std::getline(stream, line)) {
                // Process data lines
                while (std::getline(stream, line)) {
                    std::istringstream line_stream(line);
                    std::string timestamp, value_str, note;
                    
                    if (std::getline(line_stream, timestamp, ',') &&
                        std::getline(line_stream, value_str, ',')) {
                        
                        std::getline(line_stream, note); // Note is optional
                        
                        try {
                            double value = std::stod(value_str);
                            kpi_data_point data_point;
                            data_point.timestamp = timestamp;
                            data_point.value = value;
                            data_point.note = note;
                            data_point.source = "csv_import";
                            
                            if (repository_->add_kpi_data_point(kpi_id, data_point)) {
                                imported++;
                            }
                        } catch (const std::exception& e) {
                            KPI_WARN_FMT("Failed to parse CSV line: {}", line);
                        }
                    }
                }
            }
            
            KPI_INFO_FMT("Imported {} data points from CSV for KPI: {}", imported, kpi_id);
            return imported > 0;
        } catch (const std::exception& e) {
            KPI_ERROR_FMT("Error importing CSV data: {}", e.what());
            return false;
        }
    });
}

// Integration helpers
std::future<bool> kpi_model::sync_with_jira(const std::string& kpi_id, const std::string& jql_query) {
    return std::async(std::launch::async, [this, kpi_id, jql_query]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        try {
            // TODO: Implement JIRA integration
            // This would require access to the JIRA model/API
            KPI_INFO_FMT("JIRA sync requested for KPI: {} with query: {}", kpi_id, jql_query);
            
            // Placeholder implementation
            return true;
        } catch (const std::exception& e) {
            KPI_ERROR_FMT("Error syncing with JIRA: {}", e.what());
            return false;
        }
    });
}

std::future<bool> kpi_model::sync_with_external_api(const std::string& kpi_id, const std::string& api_endpoint, const std::string& value_path) {
    return std::async(std::launch::async, [this, kpi_id, api_endpoint, value_path]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        try {
            // TODO: Implement external API integration
            // This would require HTTP client functionality
            KPI_INFO_FMT("External API sync requested for KPI: {} endpoint: {} path: {}", kpi_id, api_endpoint, value_path);
            
            // Placeholder implementation
            return true;
        } catch (const std::exception& e) {
            KPI_ERROR_FMT("Error syncing with external API: {}", e.what());
            return false;
        }
    });
}

// Utility methods
std::string kpi_model::generate_id() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    return std::format("kpi_{}{}", timestamp, dis(gen));
}

std::string kpi_model::format_value(double value, kpi_value_type type, const std::string& unit) {
    std::ostringstream oss;
    
    switch (type) {
        case kpi_value_type::percentage:
            oss << std::fixed << std::setprecision(1) << value << "%";
            break;
        case kpi_value_type::currency:
            oss << "$" << std::fixed << std::setprecision(2) << value;
            break;
        case kpi_value_type::count:
            oss << static_cast<long long>(value);
            break;
        case kpi_value_type::ratio:
            oss << std::fixed << std::setprecision(2) << value << ":1";
            break;
        case kpi_value_type::boolean:
            oss << (value > 0.5 ? "Yes" : "No");
            break;
        case kpi_value_type::text:
            oss << static_cast<long long>(value); // Text values stored as codes
            break;
        default:
            oss << std::fixed << std::setprecision(2) << value;
            break;
    }
    
    if (!unit.empty() && type != kpi_value_type::percentage && type != kpi_value_type::currency) {
        oss << " " << unit;
    }
    
    return oss.str();
}

std::string kpi_model::get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::seconds>(now));
}

kpi_trend kpi_model::calculate_trend(const std::vector<kpi_data_point>& history, int periods) {
    if (history.size() < 2) {
        return kpi_trend::unknown;
    }
    
    // Sort history by timestamp (newest first)
    auto sorted_history = history;
    std::sort(sorted_history.begin(), sorted_history.end(), 
              [](const kpi_data_point& a, const kpi_data_point& b) {
                  return a.timestamp > b.timestamp;
              });
    
    // Take the specified number of recent periods
    int count = std::min(periods, static_cast<int>(sorted_history.size()));
    if (count < 2) {
        return kpi_trend::unknown;
    }
    
    double recent_avg = 0.0;
    double previous_avg = 0.0;
    
    // Calculate average for recent periods
    for (int i = 0; i < count / 2; ++i) {
        recent_avg += sorted_history[i].value;
    }
    recent_avg /= (count / 2);
    
    // Calculate average for previous periods
    for (int i = count / 2; i < count; ++i) {
        previous_avg += sorted_history[i].value;
    }
    previous_avg /= (count - count / 2);
    
    double change_threshold = 0.05; // 5% change threshold
    double relative_change = std::abs(recent_avg - previous_avg) / previous_avg;
    
    if (relative_change < change_threshold) {
        return kpi_trend::stable;
    } else if (recent_avg > previous_avg) {
        return kpi_trend::up;
    } else {
        return kpi_trend::down;
    }
}

double kpi_model::calculate_trend_percentage(const std::vector<kpi_data_point>& history, int periods) {
    if (history.size() < 2) {
        return 0.0;
    }
    
    // Sort history by timestamp (newest first)
    auto sorted_history = history;
    std::sort(sorted_history.begin(), sorted_history.end(), 
              [](const kpi_data_point& a, const kpi_data_point& b) {
                  return a.timestamp > b.timestamp;
              });
    
    int count = std::min(periods, static_cast<int>(sorted_history.size()));
    if (count < 2) {
        return 0.0;
    }
    
    double recent_value = sorted_history[0].value;
    double previous_value = sorted_history[count - 1].value;
    
    if (previous_value == 0.0) {
        return 0.0;
    }
    
    return ((recent_value - previous_value) / previous_value) * 100.0;
}

// Helper methods
void kpi_model::calculate_derived_fields(kpi& target_kpi) {
    // Calculate trend
    target_kpi.trend = calculate_trend(target_kpi.history);
    target_kpi.trend_percentage = calculate_trend_percentage(target_kpi.history);
    
    // Calculate target status
    if (target_kpi.target) {
        if (target_kpi.target->is_upper_bound) {
            if (target_kpi.current_value <= target_kpi.target->value) {
                target_kpi.target_status = kpi_target_status::on_target;
            } else {
                target_kpi.target_status = kpi_target_status::above_target;
            }
        } else {
            if (target_kpi.current_value >= target_kpi.target->value) {
                target_kpi.target_status = kpi_target_status::on_target;
            } else {
                target_kpi.target_status = kpi_target_status::below_target;
            }
        }
    } else {
        target_kpi.target_status = kpi_target_status::no_target;
    }
}

bool kpi_model::validate_kpi(const kpi& target_kpi) {
    if (target_kpi.id.empty() || target_kpi.name.empty()) {
        KPI_ERROR("KPI must have ID and name");
        return false;
    }
    
    if (target_kpi.value_type < kpi_value_type::number || target_kpi.value_type > kpi_value_type::text) {
        KPI_ERROR("Invalid KPI value type");
        return false;
    }
    
    if (target_kpi.aggregation_type < kpi_aggregation::sum || target_kpi.aggregation_type > kpi_aggregation::custom) {
        KPI_ERROR("Invalid KPI aggregation type");
        return false;
    }
    
    return true;
}

void kpi_model::update_parent_aggregations(const std::string& kpi_id) {
    try {
        auto parent_opt = repository_->get_kpi(kpi_id);
        if (!parent_opt) {
            return;
        }
        
        auto& parent = *parent_opt;
        if (parent.child_ids.empty()) {
            return;
        }
        
        // Calculate new aggregated value
        auto aggregations = calculate_aggregations(kpi_id).get();
        if (aggregations.find("value") != aggregations.end()) {
            parent.current_value = aggregations["value"];
            parent.last_updated = get_current_timestamp();
            repository_->update_kpi(parent);
            
            // Recursively update parent's parent
            if (!parent.parent_id.empty()) {
                update_parent_aggregations(parent.parent_id);
            }
        }
    } catch (const std::exception& e) {
        KPI_ERROR_FMT("Error updating parent aggregations for {}: {}", kpi_id, e.what());
    }
}

double kpi_model::apply_aggregation(kpi_aggregation type, const std::vector<double>& values, const std::map<std::string, double>& weights) {
    if (values.empty()) {
        return 0.0;
    }
    
    switch (type) {
        case kpi_aggregation::sum:
            return std::accumulate(values.begin(), values.end(), 0.0);
            
        case kpi_aggregation::average:
            return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
            
        case kpi_aggregation::weighted_avg: {
            if (weights.empty()) {
                return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
            }
            double weighted_sum = 0.0;
            double total_weight = 0.0;
            for (size_t i = 0; i < values.size(); ++i) {
                auto weight_key = std::to_string(i);
                double weight = weights.find(weight_key) != weights.end() ? weights.at(weight_key) : 1.0;
                weighted_sum += values[i] * weight;
                total_weight += weight;
            }
            return total_weight > 0 ? weighted_sum / total_weight : 0.0;
        }
        
        case kpi_aggregation::min:
            return *std::min_element(values.begin(), values.end());
            
        case kpi_aggregation::max:
            return *std::max_element(values.begin(), values.end());
            
        case kpi_aggregation::count:
            return static_cast<double>(values.size());
            
        case kpi_aggregation::custom:
        default:
            // For custom aggregation, use average as default
            return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    }
}

} // namespace rouen::models
