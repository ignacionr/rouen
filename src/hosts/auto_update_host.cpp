#include "auto_update_host.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <curl/curl.h>
#include <cstdlib>

namespace rouen::hosts {

AutoUpdateHost::AutoUpdateHost() {
    // Read environment variable ROUEN_AUTO_UPDATE_ENABLED if present
    if (const char* env_val = std::getenv("ROUEN_AUTO_UPDATE_ENABLED")) {
        std::string v(env_val);
        if (v == "0" || v == "false" || v == "FALSE") {
            enabled_ = false;
        }
    }
    status_.enabled = enabled_;
    status_.current_version = "1.4.19"; // Default runtime version
    if (const char* ver_env = std::getenv("ROUEN_VERSION")) {
        status_.current_version = ver_env;
    }
}

AutoUpdateHost::~AutoUpdateHost() {
    stop();
}

void AutoUpdateHost::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_thread_ = std::jthread([this]([[maybe_unused]] std::stop_token stoken) {
        run_loop();
    });
}

void AutoUpdateHost::stop() {
    running_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.request_stop();
    }
}

bool AutoUpdateHost::is_enabled() const {
    return enabled_;
}

void AutoUpdateHost::set_enabled(bool enabled) {
    enabled_ = enabled;
    std::lock_guard lock(mutex_);
    status_.enabled = enabled;
}

AutoUpdateStatus AutoUpdateHost::get_status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

bool AutoUpdateHost::parse_semver(std::string const& version_str, int& major, int& minor, int& patch) {
    major = 0;
    minor = 0;
    patch = 0;
    std::string s = version_str;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) {
        s = s.substr(1);
    }
    // Remove pre-release suffix if any (e.g. -beta1)
    auto dash_pos = s.find('-');
    if (dash_pos != std::string::npos) {
        s = s.substr(0, dash_pos);
    }
    std::stringstream ss(s);
    char delim1 = 0, delim2 = 0;
    if (!(ss >> major)) return false;
    if (ss >> delim1 && delim1 == '.') {
        if (!(ss >> minor)) return false;
        if (ss >> delim2 && delim2 == '.') {
            if (!(ss >> patch)) return false;
        }
    }
    return true;
}

bool AutoUpdateHost::compare_versions(std::string const& local_ver, std::string const& remote_ver) {
    int l_maj = 0, l_min = 0, l_pat = 0;
    int r_maj = 0, r_min = 0, r_pat = 0;
    if (!parse_semver(local_ver, l_maj, l_min, l_pat)) return false;
    if (!parse_semver(remote_ver, r_maj, r_min, r_pat)) return false;

    if (r_maj != l_maj) return r_maj > l_maj;
    if (r_min != l_min) return r_min > l_min;
    return r_pat > l_pat;
}

std::string AutoUpdateHost::select_platform_asset(std::string const& release_json) {
#if defined(_WIN32)
    std::string target_suffix = "rouen-windows-x64.zip";
#elif defined(__APPLE__)
    std::string target_suffix = "rouen-macos-universal.dmg";
#else
    std::string target_suffix = "rouen-linux-x64.tar.gz";
#endif

    size_t pos = 0;
    while ((pos = release_json.find(target_suffix, pos)) != std::string::npos) {
        auto q_start = release_json.rfind('"', pos);
        auto q_end = release_json.find('"', pos + target_suffix.size());
        if (q_start != std::string::npos && q_end != std::string::npos) {
            return release_json.substr(q_start + 1, q_end - q_start - 1);
        }
        pos += target_suffix.size();
    }
    return "";
}

std::chrono::system_clock::time_point AutoUpdateHost::calculate_next_check_time(
    std::chrono::system_clock::time_point now,
    int window_start_hour,
    int window_max_minutes,
    int* out_offset_minutes
) {
    auto const time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now = *std::localtime(&time_t_now);

    // Set to midnight (window_start_hour:00:00) of today
    tm_now.tm_hour = window_start_hour;
    tm_now.tm_min = 0;
    tm_now.tm_sec = 0;
    auto midnight_today = std::chrono::system_clock::from_time_t(std::mktime(&tm_now));

    // If midnight today has already passed, use midnight tomorrow
    if (now >= midnight_today + std::chrono::hours(24)) {
        tm_now.tm_mday += 1;
        midnight_today = std::chrono::system_clock::from_time_t(std::mktime(&tm_now));
    } else if (now >= midnight_today + std::chrono::hours(window_start_hour + 3)) {
        tm_now.tm_mday += 1;
        midnight_today = std::chrono::system_clock::from_time_t(std::mktime(&tm_now));
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, window_max_minutes);
    int offset_mins = dis(gen);
    if (out_offset_minutes) {
        *out_offset_minutes = offset_mins;
    }

    return midnight_today + std::chrono::minutes(offset_mins);
}

void AutoUpdateHost::run_loop() {
    while (running_) {
        if (!enabled_) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            continue;
        }

        int offset_mins = 0;
        auto now = std::chrono::system_clock::now();
        auto next_check = calculate_next_check_time(now, 0, 180, &offset_mins);

        {
            std::lock_guard lock(mutex_);
            auto const tt = std::chrono::system_clock::to_time_t(next_check);
            std::stringstream ss;
            ss << std::put_time(std::localtime(&tt), "%Y-%m-%d %H:%M:%S");
            status_.next_check_at = ss.str();
            status_.random_offset_minutes = offset_mins;
        }

        auto sleep_duration = next_check - std::chrono::system_clock::now();
        if (sleep_duration < std::chrono::seconds(0)) {
            sleep_duration = std::chrono::seconds(1);
        }

        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(sleep_duration);
        while (running_ && remaining > std::chrono::seconds(0)) {
            auto chunk = std::min(remaining, std::chrono::seconds(5));
            std::this_thread::sleep_for(chunk);
            remaining -= chunk;
        }

        if (running_ && enabled_) {
            check_update_impl();
        }
    }
}

bool AutoUpdateHost::check_for_updates_now([[maybe_unused]] std::string* error_out) {
    return check_update_impl();
}

bool AutoUpdateHost::check_update_impl() {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string response_data;
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/ignacionr/rouen/releases/latest");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RouenAutoUpdate/1.4");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* ptr, size_t size, size_t nmemb, std::string* data) {
        size_t total = size * nmemb;
        data->append(ptr, total);
        return total;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::lock_guard lock(mutex_);
        status_.last_check_result = "network_error";
        return false;
    }

    auto tag_pos = response_data.find("\"tag_name\"");
    if (tag_pos == std::string::npos) {
        std::lock_guard lock(mutex_);
        status_.last_check_result = "parse_error";
        return false;
    }
    auto q1 = response_data.find('"', tag_pos + 10);
    if (q1 == std::string::npos) return false;
    auto q2 = response_data.find('"', q1 + 1);
    if (q2 == std::string::npos) return false;
    std::string remote_tag = response_data.substr(q1 + 1, q2 - (q1 + 1));

    {
        std::lock_guard lock(mutex_);
        status_.latest_known_version = remote_tag;
        auto const tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::stringstream ss;
        ss << std::put_time(std::localtime(&tt), "%Y-%m-%d %H:%M:%S");
        status_.last_check_at = ss.str();

        if (compare_versions(status_.current_version, remote_tag)) {
            status_.last_check_result = "update_available";
            std::string asset_url = select_platform_asset(response_data);
            if (!asset_url.empty()) {
                // Pre-flight staging can be invoked here
            }
            return true;
        } else {
            status_.last_check_result = "up_to_date";
            return false;
        }
    }
}

} // namespace rouen::hosts
