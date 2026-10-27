#pragma once

#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <functional>
#include <optional>
#include <random>

namespace rouen::hosts {

struct AutoUpdateStatus {
    bool enabled{true};
    std::string current_version{"1.4.19"};
    std::string latest_known_version{};
    std::string last_check_at{};
    std::string next_check_at{};
    int random_offset_minutes{0};
    std::string last_check_result{"idle"};
};

class AutoUpdateHost {
public:
    AutoUpdateHost();
    ~AutoUpdateHost();

    void start();
    void stop();

    bool is_enabled() const;
    void set_enabled(bool enabled);

    AutoUpdateStatus get_status() const;
    bool check_for_updates_now(std::string* error_out = nullptr);

    // Static helper functions testable independently
    static bool parse_semver(std::string const& version_str, int& major, int& minor, int& patch);
    static bool compare_versions(std::string const& local_ver, std::string const& remote_ver);
    static std::string select_platform_asset(std::string const& release_json);
    static std::chrono::system_clock::time_point calculate_next_check_time(
        std::chrono::system_clock::time_point now,
        int window_start_hour = 0,
        int window_max_minutes = 180,
        int* out_offset_minutes = nullptr
    );

private:
    void run_loop();
    bool check_update_impl();

    std::atomic<bool> enabled_{true};
    std::atomic<bool> running_{false};
    std::jthread worker_thread_;
    
    mutable std::mutex mutex_;
    AutoUpdateStatus status_;
};

} // namespace rouen::hosts
