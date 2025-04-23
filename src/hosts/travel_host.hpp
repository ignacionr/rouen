#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../models/travel/plan.hpp"
#include "../models/travel/sqliterepo.hpp"
#include "../helpers/debug.hpp" // Add debug header

namespace rouen::hosts {

class TravelHost {
public:
    TravelHost(std::function<std::string(std::string_view)> system_runner) 
        : system_runner_(system_runner), 
          repo_("travel.db"),
          initialized_(false),
          initializing_(false) {
        DB_INFO("TravelHost: Initializing travel host");
        
        // Start initialization in a separate thread to avoid blocking
        std::thread init_thread([this] {
            initialize_async();
        });
        init_thread.detach(); // Let it run independently
    }
    
    // Asynchronous initialization to avoid blocking the UI thread
    void initialize_async() {
        if (initializing_.exchange(true)) {
            // Another thread is already initializing
            return;
        }
        
        try {
            DB_INFO("TravelHost: Starting async initialization");
            
            // Debug to track if we're actually reaching this point
            DB_INFO("TravelHost: About to query database");
            
            // First try a simple query to make sure SQLite is working
            // This helps debug if there's an underlying database issue
            try {
                repo_.execute_test_query();
                DB_INFO("TravelHost: Test query succeeded");
            } catch (const std::exception& e) {
                DB_ERROR_FMT("TravelHost: Test query failed: {}", e.what());
                // Continue anyway to try the full initialization
            }
            
            // Create a temporary vector to hold plans
            std::vector<std::shared_ptr<media::travel::plan>> new_plans;
            
            // Load the plans from the database
            DB_INFO("TravelHost: Loading plans from database asynchronously");
            
            // Try to catch any specific issues during the scan_plans call
            try {
                repo_.scan_plans([this, &new_plans](long long id, const char* title, const char* start_date, 
                                 const char* end_date, const char* status) {
                    DB_INFO_FMT("TravelHost: Loading plan {} from database", id);
                    
                    try {
                        auto plan = std::make_shared<media::travel::plan>();
                        if (repo_.get_plan(id, *plan)) {
                            new_plans.push_back(plan);
                        } else {
                            DB_WARN_FMT("TravelHost: Failed to load plan {} details", id);
                        }
                    } catch (const std::exception& e) {
                        DB_ERROR_FMT("TravelHost: Exception loading plan {}: {}", id, e.what());
                    }
                });
                
                DB_INFO_FMT("TravelHost: Loaded {} plans during scan", new_plans.size());
            } catch (const std::exception& e) {
                DB_ERROR_FMT("TravelHost: Exception during scan_plans: {}", e.what());
            }
            
            // Update the travel plans vector
            {
                std::lock_guard<std::mutex> lock(plans_mutex_);
                travel_plans_ = std::move(new_plans);
                DB_INFO_FMT("TravelHost: Updated travel_plans_ to contain {} plans", travel_plans_.size());
            }
            
            DB_INFO_FMT("TravelHost: Async initialization complete, loaded {} plans", travel_plans_.size());
            initialized_.store(true);
        } catch (const std::exception& e) {
            DB_ERROR_FMT("TravelHost: Exception during async initialization: {}", e.what());
            // Mark as initialized anyway to prevent permanent waiting
            initialized_.store(true);
        } catch (...) {
            DB_ERROR("TravelHost: Unknown exception during async initialization");
            // Mark as initialized anyway to prevent permanent waiting
            initialized_.store(true);
        }
        
        // Always mark initialization as done to prevent deadlocks
        initializing_.store(false);
    }

    // Create a new travel plan
    long long createPlan(
        const std::string& title, 
        const std::string& description,
        const std::chrono::system_clock::time_point& start_date,
        const std::chrono::system_clock::time_point& end_date,
        double total_budget = 0.0
    ) {
        DB_INFO_FMT("TravelHost: Creating new plan '{}'", title);
        
        // Make sure we're initialized
        ensure_initialized();
        
        media::travel::plan plan;
        plan.title = title;
        plan.description = description;
        plan.created = std::chrono::system_clock::now();
        plan.start_date = start_date;
        plan.end_date = end_date;
        plan.total_budget = total_budget;
        plan.current_status = media::travel::plan::status::planning;
        
        plan.id = repo_.upsert_plan(plan);
        
        // Refresh our cached plans
        std::thread refresh_thread([this] {
            refresh();
        });
        refresh_thread.detach();
        
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
        DB_INFO_FMT("TravelHost: Adding destination '{}' to plan {}", name, plan_id);
        
        // Make sure we're initialized
        ensure_initialized();
        
        media::travel::plan plan;
        if (!repo_.get_plan(plan_id, plan)) {
            DB_ERROR_FMT("TravelHost: Failed to get plan {} for adding destination", plan_id);
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
        
        // Refresh plans in background
        std::thread refresh_thread([this] {
            refresh();
        });
        refresh_thread.detach();
        
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
        DB_INFO_FMT("TravelHost: Updating plan {}", plan_id);
        
        // Make sure we're initialized
        ensure_initialized();
        
        media::travel::plan plan;
        if (!repo_.get_plan(plan_id, plan)) {
            DB_ERROR_FMT("TravelHost: Failed to get plan {} for updating", plan_id);
            return false;
        }
        
        plan.title = title;
        plan.description = description;
        plan.start_date = start_date;
        plan.end_date = end_date;
        plan.current_status = status;
        plan.total_budget = total_budget;
        
        repo_.upsert_plan(plan);
        
        // Refresh plans in background
        std::thread refresh_thread([this] {
            refresh();
        });
        refresh_thread.detach();
        
        return true;
    }

    // Delete a travel plan
    bool deletePlan(long long plan_id) {
        DB_INFO_FMT("TravelHost: Deleting plan {}", plan_id);
        
        // Make sure we're initialized
        ensure_initialized();
        
        repo_.delete_plan(plan_id);
        
        // Refresh plans in background
        std::thread refresh_thread([this] {
            refresh();
        });
        refresh_thread.detach();
        
        return true;
    }

    // Get a specific travel plan by ID
    std::shared_ptr<media::travel::plan> getPlan(long long plan_id) {
        DB_INFO_FMT("TravelHost: Getting plan {}", plan_id);
        
        // Make sure we're initialized
        ensure_initialized();
        
        try {
            std::lock_guard<std::mutex> lock(plans_mutex_);
            
            auto it = std::find_if(travel_plans_.begin(), travel_plans_.end(), 
                [plan_id](const auto& plan) { return plan->id == plan_id; });
            
            if (it != travel_plans_.end()) {
                DB_INFO_FMT("TravelHost: Found plan {} in cache", plan_id);
                return *it;
            }
            
            DB_INFO_FMT("TravelHost: Plan {} not found in cache", plan_id);
            
            // If not found in cache, try loading directly from database
            auto plan = std::make_shared<media::travel::plan>();
            if (repo_.get_plan(plan_id, *plan)) {
                return plan;
            }
            
            return nullptr;
        } catch (const std::exception& e) {
            DB_ERROR_FMT("TravelHost: Exception in getPlan: {}", e.what());
            return nullptr;
        }
    }

    // Get all travel plans
    std::vector<std::shared_ptr<media::travel::plan>> plans() {
        DB_INFO("TravelHost: Getting all plans");
        
        // Make sure we're initialized
        ensure_initialized();
        
        try {
            std::lock_guard<std::mutex> lock(plans_mutex_);
            DB_INFO_FMT("TravelHost: Returning {} plans", travel_plans_.size());
            return travel_plans_; // Return a copy to avoid threading issues
        } catch (const std::exception& e) {
            DB_ERROR_FMT("TravelHost: Exception in plans(): {}", e.what());
            return {};
        }
    }

    // Direct update of a plan (used for destination completion toggling)
    bool updatePlanDirectly(const media::travel::plan& plan) {
        DB_INFO_FMT("TravelHost: Directly updating plan {}", plan.id);
        
        // Make sure we're initialized
        ensure_initialized();
        
        repo_.upsert_plan(const_cast<media::travel::plan&>(plan));
        
        // Refresh plans in background
        std::thread refresh_thread([this] {
            refresh();
        });
        refresh_thread.detach();
        
        return true;
    }

    // Refresh the travel plans list from the database
    void refresh() {
        DB_INFO("TravelHost: Refreshing plans from database");
        try {
            // Create a temporary vector to hold the new plans
            std::vector<std::shared_ptr<media::travel::plan>> new_plans;
            
            // Load plans from the database
            DB_INFO("TravelHost: Loading plans from database for refresh");
            repo_.scan_plans([this, &new_plans](long long id, const char* title, const char* start_date, 
                             const char* end_date, const char* status) {
                DB_INFO_FMT("TravelHost: Loading plan {} from database", id);
                auto plan = std::make_shared<media::travel::plan>();
                repo_.get_plan(id, *plan);
                new_plans.push_back(plan);
            });
            
            // Now acquire the mutex only for the quick vector swap operation
            {
                std::lock_guard<std::mutex> lock(plans_mutex_);
                DB_INFO("TravelHost: Acquired lock for swapping plan vectors");
                travel_plans_.swap(new_plans);
            }
            
            DB_INFO_FMT("TravelHost: Refresh complete, loaded {} plans", travel_plans_.size());
        } catch (const std::exception& e) {
            DB_ERROR_FMT("TravelHost: Exception in refresh: {}", e.what());
        }
    }

    // Get access to the Travel host controller (needed for travel plan cards)
    static std::shared_ptr<TravelHost> getHost() {
        try {
            static std::weak_ptr<TravelHost> weak_host;
            static std::mutex init_mutex;
            
            std::lock_guard<std::mutex> lock(init_mutex);
            
            // Try to get a shared_ptr from the weak_ptr
            auto shared_host = weak_host.lock();
            
            // If the weak_ptr has expired or was never initialized, create a new instance
            if (!shared_host) {
                DB_INFO("TravelHost: Creating new shared TravelHost instance");
                shared_host = std::make_shared<TravelHost>([](std::string_view cmd) -> std::string {
                    return ""; // Not using system commands in this implementation
                });
                weak_host = shared_host; // Store a weak_ptr, not keeping the object alive
            }
            
            return shared_host;
        } catch (const std::exception& e) {
            DB_ERROR_FMT("TravelHost: Exception in getHost: {}", e.what());
            // Fallback to creating a new instance in case of failure
            return std::make_shared<TravelHost>([](std::string_view cmd) -> std::string {
                return "";
            });
        }
    }

private:
    // Helper method to ensure we're initialized
    void ensure_initialized() {
        if (!initialized_.load()) {
            DB_INFO("TravelHost: Waiting for initialization to complete");
            
            // Try to initialize if not already doing so
            if (!initializing_.load()) {
                DB_INFO("TravelHost: Starting initialization since it wasn't running");
                initialize_async();
            }
            
            // Wait for initialization with a longer timeout (5 seconds total)
            // This is important because SQLite operations might be slower than expected
            int attempts = 0;
            const int max_attempts = 100; // 100 * 50ms = 5 seconds
            
            while (!initialized_.load() && attempts < max_attempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                attempts++;
                
                // Log progress every second
                if (attempts % 20 == 0) {
                    DB_INFO_FMT("TravelHost: Still waiting for initialization, attempt {}/{}", 
                               attempts, max_attempts);
                }
            }
            
            if (!initialized_.load()) {
                DB_WARN("TravelHost: Initialization timeout, proceeding with empty plans");
                // Force initialization to true to prevent further waits
                initialized_.store(true);
                
                // Try one final direct initialization as a fallback
                try {
                    DB_INFO("TravelHost: Attempting direct initialization as fallback");
                    std::vector<std::shared_ptr<media::travel::plan>> empty_plans;
                    std::lock_guard<std::mutex> lock(plans_mutex_);
                    travel_plans_ = std::move(empty_plans);
                } catch (const std::exception& e) {
                    DB_ERROR_FMT("TravelHost: Fallback initialization failed: {}", e.what());
                }
            }
        }
    }

    std::function<std::string(std::string_view)> system_runner_;
    media::travel::sqliterepo repo_;
    std::vector<std::shared_ptr<media::travel::plan>> travel_plans_;
    std::mutex plans_mutex_;
    std::atomic<bool> initialized_;    // Indicates if initial loading is complete
    std::atomic<bool> initializing_;   // Indicates if initialization is in progress
};

} // namespace rouen::hosts