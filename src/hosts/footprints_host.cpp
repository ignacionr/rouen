#include "footprints_host.hpp"

// 1. Standard includes in alphabetic order
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
#include <glaze/glaze.hpp>

// 3. All other includes
#include "../helpers/debug.hpp"
#include "../helpers/fetch.hpp"
#include "../helpers/platform_utils.hpp"

#define FP_HOST_ERROR(message) LOG_COMPONENT("FOOTPRINTS_HOST", LOG_LEVEL_ERROR, message)
#define FP_HOST_INFO(message) LOG_COMPONENT("FOOTPRINTS_HOST", LOG_LEVEL_INFO, message)
#define FP_HOST_ERROR_FMT(fmt, ...) FP_HOST_ERROR(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::hosts {

// Only what gets persisted to disk. Deliberately has no password field.
// Must have external linkage (not in an anonymous namespace) for glaze reflection.
struct footprints_saved_profile {
    std::string base_url;
    std::string username;
    std::string remember_cookie;
};

namespace {
    std::string url_encode(const std::string& value) {
        std::ostringstream out;
        out.fill('0');
        for (char raw_c : value) {
            auto c{static_cast<unsigned char>(raw_c)};
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out << static_cast<char>(c);
            } else {
                out << '%' << std::uppercase << std::hex << std::setw(2) << static_cast<int>(c) << std::nouppercase << std::dec;
            }
        }
        return out.str();
    }

    std::string trim(const std::string& s) {
        size_t start{0};
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
        size_t end{s.size()};
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
        return s.substr(start, end - start);
    }

    // A raw "Set-Cookie" value looks like "name=value; Path=/; HttpOnly". Extracts
    // the value for a given cookie name from a list of such headers, if present.
    std::optional<std::string> extract_cookie(const std::vector<std::string>& set_cookie_headers, const std::string& name) {
        for (const auto& header : set_cookie_headers) {
            auto semi = header.find(';');
            std::string first_pair{semi == std::string::npos ? header : header.substr(0, semi)};
            auto eq = first_pair.find('=');
            if (eq == std::string::npos) continue;
            if (trim(first_pair.substr(0, eq)) == name) {
                return first_pair.substr(eq + 1);
            }
        }
        return std::nullopt;
    }

    std::filesystem::path profile_path() {
        return rouen::platform::get_user_data_path("footprints_profile.json");
    }

    void write_profile(const std::string& base_url, const std::string& username, const std::string& remember_cookie) {
        footprints_saved_profile data{base_url, username, remember_cookie};
        std::string json;
        auto err = glz::write_json(data, json);
        if (err) {
            FP_HOST_ERROR("Failed to serialize FootPrints profile");
            return;
        }
        std::ofstream file{profile_path()};
        if (file.is_open()) {
            file << json;
        }
    }

    void delete_profile_file() {
        std::error_code ec;
        std::filesystem::remove(profile_path(), ec);
    }

    footprints_saved_profile load_profile() {
        footprints_saved_profile data{};
        auto path = profile_path();
        if (!std::filesystem::exists(path)) {
            return data;
        }
        std::ifstream file{path};
        if (!file.is_open()) {
            return data;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        auto err = glz::read_json(data, buffer.str());
        if (err) {
            FP_HOST_ERROR("Failed to parse saved FootPrints profile");
            return footprints_saved_profile{};
        }
        return data;
    }
}

std::shared_ptr<footprints_host> get_footprints_host() {
    static std::mutex g_mutex;
    static std::shared_ptr<footprints_host> g_host;

    std::lock_guard<std::mutex> const lock{g_mutex};
    if (!g_host) {
        g_host = std::make_shared<footprints_host>();
    }
    return g_host;
}

footprints_host::footprints_host() {
    auto saved = load_profile();
    {
        std::lock_guard<std::mutex> const lock{mutex_};
        base_url_ = saved.base_url;
        username_ = saved.username;
        remember_cookie_ = saved.remember_cookie;
    }

    if (!saved.remember_cookie.empty() && try_resume_session()) {
        FP_HOST_INFO("Resumed FootPrints session from saved Remember_Me cookie");
    }
}

void footprints_host::set_error(const std::string& error) {
    std::lock_guard<std::mutex> const lock{mutex_};
    last_error_ = error;
    FP_HOST_ERROR(error);
}

void footprints_host::clear_error() {
    std::lock_guard<std::mutex> const lock{mutex_};
    last_error_.clear();
}

bool footprints_host::login(const std::string& base_url, const std::string& username, const std::string& password, bool remember) {
    clear_error();
    if (base_url.empty() || username.empty() || password.empty()) {
        set_error("Server URL, username, and password are all required");
        return false;
    }

    std::string clean_base{base_url};
    while (!clean_base.empty() && clean_base.back() == '/') clean_base.pop_back();

    std::string body{"USER=" + url_encode(username) +
                     "&PASSWORD=" + url_encode(password) +
                     "&PROJECTID=-1&SCREEN=1"};
    if (remember) {
        body += "&REMEMBER_PASSWORD=1";
    }

    std::vector<std::string> set_cookies;
    try {
        http::fetch fetcher;
        auto header_setter = [](auto set_header) {
            set_header("Content-Type: application/x-www-form-urlencoded");
        };
        fetcher.post(clean_base + "/MRcgi/MRlogin.pl", body, header_setter);
        set_cookies = fetcher.last_set_cookies();
    } catch (const std::exception& e) {
        set_error(std::format("Login request failed: {}", e.what()));
        return false;
    }

    auto session = extract_cookie(set_cookies, "session_key");
    if (!session) {
        set_error("Login failed - check the server URL, username, and password");
        return false;
    }

    std::string remember_value;
    if (remember) {
        if (auto remember_cookie = extract_cookie(set_cookies, "Remember_Me")) {
            remember_value = *remember_cookie;
        }
    }

    {
        std::lock_guard<std::mutex> const lock{mutex_};
        base_url_ = clean_base;
        username_ = username;
        session_key_ = *session;
        remember_cookie_ = remember_value;
        connected_ = true;
    }

    if (!remember_value.empty()) {
        write_profile(clean_base, username, remember_value);
    } else {
        delete_profile_file();
    }

    FP_HOST_INFO("Connected to FootPrints");
    return true;
}

bool footprints_host::try_resume_session() {
    std::string base_url_copy, username_copy, remember_copy;
    {
        std::lock_guard<std::mutex> const lock{mutex_};
        if (remember_cookie_.empty() || base_url_.empty() || username_.empty()) {
            return false;
        }
        base_url_copy = base_url_;
        username_copy = username_;
        remember_copy = remember_cookie_;
    }

    clear_error();
    try {
        http::fetch fetcher;
        auto header_setter = [&remember_copy](auto set_header) {
            set_header("Cookie: Remember_Me=" + remember_copy);
        };
        std::string url{base_url_copy + "/MRcgi/MRhomepage.pl?USER=" + url_encode(username_copy) + "&PROJECTID=-1&OPTION=none"};
        std::string body{fetcher(url, header_setter)};

        bool looks_logged_in{body.find("Login Error") == std::string::npos &&
                              body.find("Access denied") == std::string::npos};
        auto session = extract_cookie(fetcher.last_set_cookies(), "session_key");

        if (looks_logged_in && session) {
            std::lock_guard<std::mutex> const lock{mutex_};
            session_key_ = *session;
            connected_ = true;
            return true;
        }

        set_error("Saved FootPrints session expired - please log in again");
        return false;
    } catch (const std::exception& e) {
        set_error(std::format("Failed to resume FootPrints session: {}", e.what()));
        return false;
    }
}

void footprints_host::logout() {
    {
        std::lock_guard<std::mutex> const lock{mutex_};
        connected_ = false;
        session_key_.clear();
        remember_cookie_.clear();
    }
    delete_profile_file();
    FP_HOST_INFO("Disconnected from FootPrints");
}

bool footprints_host::is_connected() const {
    std::lock_guard<std::mutex> const lock{mutex_};
    return connected_;
}

std::string footprints_host::username() const {
    std::lock_guard<std::mutex> const lock{mutex_};
    return username_;
}

std::string footprints_host::base_url() const {
    std::lock_guard<std::mutex> const lock{mutex_};
    return base_url_;
}

std::string footprints_host::homepage_url() const {
    std::lock_guard<std::mutex> const lock{mutex_};
    if (base_url_.empty()) return {};
    return base_url_ + "/MRcgi/MRhomepage.pl?USER=" + url_encode(username_) + "&PROJECTID=-1&OPTION=none";
}

bool footprints_host::has_saved_profile() const {
    std::lock_guard<std::mutex> const lock{mutex_};
    return !base_url_.empty() && !username_.empty();
}

} // namespace rouen::hosts
