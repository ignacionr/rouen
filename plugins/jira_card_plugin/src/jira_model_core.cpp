#include "jira_model.hpp"
#include "helpers/platform_utils.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

namespace fs = std::filesystem;

namespace rouen::models {

// Helper struct for JIRA server information
struct jira_server_info {
    std::string version;
    double build_number;
    std::string base_url;
    std::string server_title;
};

// Priority structure declaration used in implementation but not in header
struct jira_priority {
    std::string id;
    std::string name;
    std::string icon_url;
};

// Implementations of the profile state getters
std::mutex& jira_model::get_profiles_mutex() {
    static std::mutex mutex;
    return mutex;
}

bool& jira_model::get_profiles_modified() {
    static bool modified = false;
    return modified;
}

std::vector<jira_connection_profile>& jira_model::get_environment_profiles_ref() {
    static std::vector<jira_connection_profile> profiles;
    return profiles;
}

std::vector<jira_connection_profile>& jira_model::get_saved_profiles_ref() {
    static std::vector<jira_connection_profile> profiles;
    return profiles;
}

// Forward declarations of helper methods
std::string strip_trailing_slash(const std::string& url);
std::string base64_encode(const std::string& input);

// Implementation of helper functions
std::string strip_trailing_slash(const std::string& url) {
    if (!url.empty() && url.back() == '/') {
        return url.substr(0, url.length() - 1);
    }
    return url;
}

std::string base64_encode(const std::string& input) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    for (char const c : input) {
        char_array_3[i++] = static_cast<unsigned char>(c);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = static_cast<unsigned char>(((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4));
            char_array_4[2] = static_cast<unsigned char>(((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6));
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; (i <4) ; i++)
                result += chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(int j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = static_cast<unsigned char>(((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4));
        char_array_4[2] = static_cast<unsigned char>(((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6));
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (int j = 0; (j < i + 1); j++)
            result += chars[char_array_4[j]];

        while((i++ < 3))
            result += '=';
    }

    return result;
}

std::filesystem::path jira_model::get_profiles_path() {
    return rouen::platform::get_user_data_path("jira_profiles.json", true);
}

jira_model::jira_model() = default;

jira_model::~jira_model() {
    // Save profiles if modified
    if (get_profiles_modified()) {
        save_profiles(get_saved_profiles_ref());
    }
}

bool jira_model::is_connected() const {
    return connected_;
}

std::string jira_model::get_server_url() const {
    return connected_ ? current_profile_.server_url : "";
}

std::string jira_model::get_current_profile_name() const {
    return connected_ ? current_profile_.name : "";
}

jira_connection_profile jira_model::get_current_profile() const {
    return current_profile_;
}

} // namespace rouen::models
