#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include "../models/travel/plan.hpp"
#include "../models/travel/sqliterepo.hpp"

namespace rouen::hosts {

class TravelHost {
public:
    TravelHost(std::function<std::string(std::string_view)> system_runner) 
        : system_runner_(system_runner), 
          repo_("travel.db") {
        refresh();
    }

    // Create a new travel plan
    long long createPlan(
        const std::string& title, 
        const std::string& description,
        const std::chrono::system_clock::time_point& start_date,
        const std::chrono::system_clock::time_point& end_date,
        double total_budget = 0.0
    ) {
        media::travel::plan plan;
        plan.title = title;
        plan.description = description;
        plan.created = std::chrono::system_clock::now();
        plan.start_date = start_date;
        plan.end_date = end_date;
        plan.total_budget = total_budget;
        plan.current_status = media::travel::plan::status::planning;
        
        plan.id = repo_.upsert_plan(plan);
        refresh();
        return plan.id;
    }

    // Add a destination to a travel plan
    bool addDestination(
        long long plan_id,
        const std::string& name,
        const std::string& location,
        const std::chrono::system_clock::time_point& arrival,
        const std::chrono::system_clock::time_point& departure,
        const std::string& accommodation = "",
        const std::string& notes = "",
        double budget = 0.0
    ) {
        media::travel::plan plan;
        if (!repo_.get_plan(plan_id, plan)) {
            return false;
        }
        
        media::travel::destination dest;
        dest.name = name;
        dest.location = location;
        dest.arrival = arrival;
        dest.departure = departure;
        dest.accommodation = accommodation;
        dest.notes = notes;
        dest.budget = budget;
        dest.completed = false;
        
        plan.destinations.push_back(dest);
        plan.total_budget += budget; // Update total budget
        
        repo_.upsert_plan(plan);
        refresh();
        return true;
    }

    // Update an existing travel plan
    bool updatePlan(
        long long plan_id,
        const std::string& title,
        const std::string& description,
        const std::chrono::system_clock::time_point& start_date,
        const std::chrono::system_clock::time_point& end_date,
        media::travel::plan::status status,
        double total_budget
    ) {
        media::travel::plan plan;
        if (!repo_.get_plan(plan_id, plan)) {
            return false;
        }
        
        plan.title = title;
        plan.description = description;
        plan.start_date = start_date;
        plan.end_date = end_date;
        plan.current_status = status;
        plan.total_budget = total_budget;
        
        repo_.upsert_plan(plan);
        refresh();
        return true;
    }

    // Delete a travel plan
    bool deletePlan(long long plan_id) {
        repo_.delete_plan(plan_id);
        refresh();
        return true;
    }

    // Get a specific travel plan by ID
    std::shared_ptr<media::travel::plan> getPlan(long long plan_id) {
        auto it = std::find_if(travel_plans_.begin(), travel_plans_.end(), 
            [plan_id](const auto& plan) { return plan->id == plan_id; });
        
        if (it != travel_plans_.end()) {
            return *it;
        }
        
        return nullptr;
    }

    // Get all travel plans
    const std::vector<std::shared_ptr<media::travel::plan>>& plans() {
        return travel_plans_;
    }

    // Direct update of a plan (used for destination completion toggling)
    bool updatePlanDirectly(const media::travel::plan& plan) {
        repo_.upsert_plan(const_cast<media::travel::plan&>(plan));
        refresh();
        return true;
    }

    // Refresh the travel plans list from the database
    void refresh() {
        travel_plans_.clear();
        
        repo_.scan_plans([this](long long id, const char* title, const char* start_date, 
                             const char* end_date, const char* status) {
            auto plan = std::make_shared<media::travel::plan>();
            repo_.get_plan(id, *plan);
            travel_plans_.push_back(plan);
        });
    }

    // Get access to the Travel host controller (needed for travel plan cards)
    static std::shared_ptr<TravelHost> getHost() {
        static std::shared_ptr<TravelHost> shared_host = nullptr;
        
        if (!shared_host) {
            shared_host = std::make_shared<TravelHost>([](std::string_view cmd) -> std::string {
                return ""; // Not using system commands in this implementation
            });
        }
        
        return shared_host;
    }

private:
    std::function<std::string(std::string_view)> system_runner_;
    media::travel::sqliterepo repo_;
    std::vector<std::shared_ptr<media::travel::plan>> travel_plans_;
};

} // namespace rouen::hosts