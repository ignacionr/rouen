#include "trello_model.hpp"

// 1. Standard includes in alphabetic order
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <glaze/core/opts.hpp>
#include <glaze/core/read.hpp>
#include <glaze/core/reflect.hpp>
#include <glaze/json/json_t.hpp>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// 2. Libraries used in the project, in alphabetic order

// 3. All other includes
#include "config_service.hpp"
#include "fetch.hpp"

namespace rouen::models::trello {

std::shared_ptr<trello_model> get_trello_model() {
    static std::mutex g_trello_model_mutex;
    std::lock_guard<std::mutex> const lock(g_trello_model_mutex);
    static std::shared_ptr<trello_model> g_trello_model;
    if (!g_trello_model) {
        g_trello_model = std::make_shared<trello_model>();
    }
    return g_trello_model;
}

// Helper function to strip trailing slash from URLs
static std::string strip_trailing_slash(const std::string& url) {
    if (!url.empty() && url.back() == '/') {
        return url.substr(0, url.length() - 1);
    }
    return url;
}

trello_model::trello_model() {
    load_saved_profiles();
    TRELLO_INFO("Trello model initialized");
}

bool trello_model::connect(const trello_connection_profile& profile) {
    current_profile_ = profile;
    connected_ = false;
    
    TRELLO_INFO_FMT("Attempting to connect to Trello with profile: {}", profile.name);
    
    try {
        // Validate profile before attempting connection
        if (profile.name.empty()) {
            throw std::runtime_error("Profile name is empty");
        }
        if (profile.api_key.empty()) {
            throw std::runtime_error("API key is empty for profile '" + profile.name + "'");
        }
        if (profile.token.empty()) {
            throw std::runtime_error("Token is empty for profile '" + profile.name + "'");
        }
        
        // Test connection by getting user information
        std::string response = test_connection_request("members/me");
        
        if (response.empty()) {
            throw std::runtime_error("Empty response from Trello API");
        }
        
        // Parse response to validate it's valid JSON
        auto json_result = glz::read_json<glz::json_t>(response);
        if (!json_result.has_value()) {
            throw std::runtime_error("Failed to parse JSON response from Trello API");
        }
        
        connected_ = true;
        TRELLO_INFO("Successfully connected to Trello:");
        TRELLO_INFO_FMT("  Profile: {}", profile.name);
        TRELLO_INFO_FMT("  API Key: {}...", profile.api_key.substr(0, 8));
        
        // Save profile if it's not from environment and not already saved
        if (!profile.is_environment) {
            std::lock_guard<std::mutex> const lock(profiles_mutex_);
            auto it = std::find_if(saved_profiles_.begin(), saved_profiles_.end(),
                                 [&profile](const auto& p) { return p.name == profile.name; });
            if (it == saved_profiles_.end()) {
                saved_profiles_.push_back(profile);
                // Save to file
                std::string profiles_json;
                auto write_result = glz::write_json(saved_profiles_, profiles_json);
                if (!write_result) {
                    std::ofstream file(get_profiles_path());
                    if (file.is_open()) {
                        file << profiles_json;
                        TRELLO_INFO_FMT("Saved profile '{}' to profiles file", profile.name);
                    }
                }
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        connected_ = false;
        std::string error_msg = std::format(
            "Failed to connect to Trello.\n"
            "Profile: '{}'\n"
            "API Key: '{}...'\n"
            "Error: {}\n"
            "\nTroubleshooting tips:\n"
            "1. Verify your API key and token are correct\n"
            "2. Check that your token hasn't expired\n"
            "3. Ensure network connectivity to api.trello.com\n"
            "4. Try regenerating your token if the issue persists",
            profile.name, profile.api_key.substr(0, 8), e.what()
        );
        TRELLO_ERROR_FMT("Connection failed: {}", error_msg);
        throw std::runtime_error(error_msg);
    }
}

bool trello_model::connect_from_environment() {
    auto config_service = rouen::helpers::ConfigService::instance();
    
    std::string const api_key = config_service->get_api_key("TRELLO");
    std::string const api_secret = config_service->get_env("TRELLO_API_SECRET");
    std::string const token = config_service->get_env("TRELLO_TOKEN");
    
    if (api_key.empty() || token.empty()) {
        TRELLO_INFO("Trello environment variables not found or incomplete");
        return false;
    }
    
    trello_connection_profile env_profile;
    env_profile.name = "Environment";
    env_profile.api_key = api_key;
    env_profile.api_secret = api_secret;
    env_profile.token = token;
    env_profile.is_environment = true;
    
    try {
        return connect(env_profile);
    } catch (const std::exception& e) {
        TRELLO_ERROR_FMT("Failed to connect using environment variables: {}", e.what());
        return false;
    }
}

void trello_model::disconnect() {
    connected_ = false;
    current_profile_ = {};
    TRELLO_INFO("Disconnected from Trello");
}

std::vector<trello_connection_profile> trello_model::get_saved_profiles() const {
    std::lock_guard<std::mutex> const lock(profiles_mutex_);
    return saved_profiles_;
}

void trello_model::save_profile(const trello_connection_profile& profile) {
    std::lock_guard<std::mutex> const lock(profiles_mutex_);
    
    // Remove existing profile with same name
    auto it = std::remove_if(saved_profiles_.begin(), saved_profiles_.end(),
                           [&profile](const auto& p) { return p.name == profile.name; });
    saved_profiles_.erase(it, saved_profiles_.end());
    
    // Add new profile
    saved_profiles_.push_back(profile);
    
    // Save to file
    std::string profiles_json;
    auto write_result = glz::write_json(saved_profiles_, profiles_json);
    if (!write_result) {
        std::ofstream file(get_profiles_path());
        if (file.is_open()) {
            file << profiles_json;
            TRELLO_INFO_FMT("Saved profile '{}' to profiles file", profile.name);
        }
    }
}

void trello_model::delete_profile(const std::string& profile_name) {
    std::lock_guard<std::mutex> const lock(profiles_mutex_);
    
    auto it = std::remove_if(saved_profiles_.begin(), saved_profiles_.end(),
                           [&profile_name](const auto& p) { return p.name == profile_name; });
    saved_profiles_.erase(it, saved_profiles_.end());
    
    // Save to file
    std::string profiles_json;
    auto write_result = glz::write_json(saved_profiles_, profiles_json);
    if (!write_result) {
        std::ofstream file(get_profiles_path());
        if (file.is_open()) {
            file << profiles_json;
            TRELLO_INFO_FMT("Deleted profile '{}' from profiles file", profile_name);
        }
    }
}

// Async API operations - Boards
std::future<std::vector<trello_board>> trello_model::get_user_boards() {
    return std::async(std::launch::async, [this]() {
        try {
            std::string const response = make_request("members/me/boards?lists=all&cards=open&labels=all&members=all");
            return parse_boards(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to get user boards: {}", e.what());
            return std::vector<trello_board>{};
        }
    });
}

std::future<trello_board> trello_model::get_board(const std::string& board_id, bool include_lists, bool include_cards) {
    return std::async(std::launch::async, [this, board_id, include_lists, include_cards]() { // NOLINT(bugprone-exception-escape)
        try {
            std::string endpoint = "boards/" + board_id + "?";
            if (include_lists) endpoint += "lists=all&";
            if (include_cards) endpoint += "cards=open&";
            endpoint += "labels=all&members=all";
            
            std::string const response = make_request(endpoint);
            return parse_board(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to get board {}: {}", board_id, e.what());
            return trello_board{};
        }
    });
}

std::future<bool> trello_model::create_board(const std::string& name, const std::string& desc) {
    return std::async(std::launch::async, [this, name, desc]() { // NOLINT(bugprone-exception-escape)
        try {
            std::string data = "name=" + name;
            if (!desc.empty()) {
                data += "&desc=" + desc;
            }
            std::string const response = make_request("boards", "POST", data);
            return !response.empty();
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to create board '{}': {}", name, e.what());
            return false;
        }
    });
}

// Async API operations - Lists
std::future<std::vector<trello_list>> trello_model::get_board_lists(const std::string& board_id) {
    return std::async(std::launch::async, [this, board_id]() { // NOLINT(bugprone-exception-escape)
        try {
            std::string const response = make_request("boards/" + board_id + "/lists");
            return parse_lists(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to get lists for board {}: {}", board_id, e.what());
            return std::vector<trello_list>{};
        }
    });
}

std::future<trello_list> trello_model::create_list(const std::string& board_id, const std::string& name, float pos) {
    return std::async(std::launch::async, [this, board_id, name, pos]() { // NOLINT(bugprone-exception-escape)
        try {
            std::string data = "name=" + name + "&idBoard=" + board_id;
            if (pos > 0) {
                data += "&pos=" + std::to_string(pos);
            }
            std::string const response = make_request("lists", "POST", data);
            return parse_list(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to create list '{}' in board {}: {}", name, board_id, e.what());
            return trello_list{};
        }
    });
}

// Async API operations - Cards
std::future<std::vector<trello_card>> trello_model::get_board_cards(const std::string& board_id) {
    return std::async(std::launch::async, [this, board_id]() { // NOLINT(bugprone-exception-escape)
        try {
            std::string const response = make_request("boards/" + board_id + "/cards");
            return parse_cards(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to get cards for board {}: {}", board_id, e.what());
            return std::vector<trello_card>{};
        }
    });
}

std::future<trello_card> trello_model::create_card(const std::string& list_id, const std::string& name, const std::string& desc, float pos) {
    return std::async(std::launch::async, [this, list_id, name, desc, pos]() { // NOLINT(bugprone-exception-escape)
        try {
            std::string data = "name=" + name + "&idList=" + list_id;
            if (!desc.empty()) {
                data += "&desc=" + desc;
            }
            if (pos > 0) {
                data += "&pos=" + std::to_string(pos);
            }
            std::string const response = make_request("cards", "POST", data);
            return parse_card(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to create card '{}' in list {}: {}", name, list_id, e.what());
            return trello_card{};
        }
    });
}

std::future<bool> trello_model::move_card(const std::string& card_id, const std::string& list_id, float pos) {
    return std::async(std::launch::async, [this, card_id, list_id, pos]() { // NOLINT(bugprone-exception-escape)
        try {
            // For Trello API, moving a card requires PUT to /1/cards/{id}
            // The request body should be form-encoded data
            std::string data = "idList=" + list_id;
            if (pos > 0) {
                data += "&pos=" + std::to_string(pos);
            }
            
            TRELLO_DEBUG_FMT("Moving card {} to list {} with data: {}", card_id, list_id, data);
            
            // Use PUT method for updating card properties (will be converted to POST internally)
            std::string response = make_request("cards/" + card_id, "PUT", data);
            
            TRELLO_DEBUG_FMT("Move card response: {}", response);
            
            // Check if we got a valid response (should contain updated card data)
            if (response.empty()) {
                TRELLO_ERROR_FMT("Empty response when moving card {} to list {}", card_id, list_id);
                return false;
            }
            
            // Try to parse the response to verify it's valid JSON
            auto json_result = glz::read_json<glz::json_t>(response);
            if (!json_result.has_value()) {
                TRELLO_ERROR_FMT("Invalid JSON response when moving card {} to list {}: {}", card_id, list_id, response);
                return false;
            }
            
            return true;
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to move card {} to list {}: {}", card_id, list_id, e.what());
            return false;
        }
    });
}

// Search functionality
std::future<std::vector<trello_card>> trello_model::search_cards(const std::string& query, const std::string& board_id) {
    return std::async(std::launch::async, [this, query, board_id]() { // NOLINT(bugprone-exception-escape)
        try {
            std::string endpoint = "search?query=" + query + "&modelTypes=cards";
            if (!board_id.empty()) {
                endpoint += "&idBoards=" + board_id;
            }
            std::string response = make_request(endpoint);
            
            // Parse search response (different format than regular cards endpoint)
            auto json_result = glz::read_json<glz::json_t>(response);
            if (!json_result.has_value()) {
                throw std::runtime_error("Failed to parse search response");
            }
            
            auto& json = json_result.value();
            if (json.contains("cards")) {
                std::string cards_json;
                auto write_result = glz::write_json(json["cards"], cards_json);
                if (!write_result) {
                    return parse_cards(cards_json);
                }
            }
            
            return std::vector<trello_card>{};
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to search cards with query '{}': {}", query, e.what());
            return std::vector<trello_card>{};
        }
    });
}

std::filesystem::path trello_model::get_profiles_file_path() {
    return get_profiles_path();
}

// Private methods

std::string trello_model::make_request(const std::string& endpoint, const std::string& method, const std::string& data) {
    if (!connected_) {
        throw std::runtime_error("Not connected to Trello");
    }

    std::string url = build_url(endpoint);

    TRELLO_DEBUG_FMT("Trello API Request: {} {}", method.empty() ? "GET" : method, url);

    http::fetch fetcher;

    std::string response;
    try {
        if (method == "POST") {
            // POST requests
            response = fetcher.post(url, data);
        } else if (method == "PUT") {
            // PUT requests - now using proper PUT method
            TRELLO_DEBUG("Using PUT method for PUT request");
            response = fetcher.put(url, data);
        } else {
            // GET request - auth already in URL from build_url()
            response = fetcher(url);
        }
        TRELLO_DEBUG_FMT("Trello API request successful to {}", url);
    } catch (const std::exception& e) {
        TRELLO_ERROR_FMT("Trello API {} request to '{}' failed: {}", 
                        method.empty() ? "GET" : method, url, e.what());
        throw;
    }

    return response;
}std::string trello_model::test_connection_request(const std::string& endpoint, const std::string& method, const std::string& data) {
    // This method is similar to make_request but bypasses the connection check
    // Used during initial connection testing
    
    std::string url = build_url(endpoint);
    
    TRELLO_DEBUG_FMT("Trello API Test Request: {} {}", method.empty() ? "GET" : method, url);
    
    http::fetch fetcher;
    
    std::string response;
    try {
        if (method == "POST" || method == "PUT") {
            // For POST/PUT requests, add auth to data
            std::string full_data = data;
            if (!full_data.empty()) full_data += "&";
            full_data += "key=" + current_profile_.api_key + "&token=" + current_profile_.token;
            
            if (method == "POST") {
                response = fetcher.post(url, full_data);
            } else {
                // PUT request
                response = fetcher.post(url, full_data, [](auto /* set_header */) {
                    // Trello API doesn't actually support PUT via POST override,
                    // but we'll handle it anyway for completeness
                });
            }
        } else {
            // GET request - auth in URL
            response = fetcher(url);
        }
        TRELLO_DEBUG_FMT("Trello API test request successful to {}", url);
    } catch (const std::exception& e) {
        TRELLO_ERROR_FMT("Trello API test {} request to '{}' failed: {}", 
                        method.empty() ? "GET" : method, url, e.what());
        throw;
    }
    
    return response;
}

std::string trello_model::build_url(const std::string& endpoint) const {
    auto config_service = rouen::helpers::ConfigService::instance();
    std::string base_url = config_service->get_env("TRELLO_HOST");
    if (base_url.empty()) {
        base_url = "https://api.trello.com";
    }
    
    base_url = strip_trailing_slash(base_url);
    std::string url = base_url + "/1/" + endpoint;
    
    // Add authentication parameters
    std::string const separator = (endpoint.find('?') != std::string::npos) ? "&" : "?";
    url += separator + "key=" + current_profile_.api_key + "&token=" + current_profile_.token;
    
    return url;
}

std::filesystem::path trello_model::get_profiles_path() {
    std::filesystem::path config_dir;
    
    // Try to get user config directory
    const char* config_home = std::getenv("XDG_CONFIG_HOME");
    if (config_home) {
        config_dir = std::filesystem::path(config_home);
    } else {
        const char* home = std::getenv("HOME");
        if (home) {
            config_dir = std::filesystem::path(home) / ".config";
        } else {
            config_dir = std::filesystem::current_path();
        }
    }
    
    config_dir /= "rouen";
    std::filesystem::create_directories(config_dir);
    
    return config_dir / "trello_profiles.json";
}

void trello_model::load_saved_profiles() {
    std::lock_guard<std::mutex> const lock(profiles_mutex_);
    
    auto profiles_path = get_profiles_path();
    if (!std::filesystem::exists(profiles_path)) {
        TRELLO_DEBUG("No saved profiles file found");
        return;
    }
    
    try {
        std::ifstream const file(profiles_path);
        if (file.is_open() && file.good()) {
            std::string json_content;
            
            // Use stringstream to safely read the entire file
            std::ostringstream buffer;
            buffer << file.rdbuf();
            json_content = buffer.str();
            
            if (!json_content.empty()) {
                auto profiles_result = glz::read_json<std::vector<trello_connection_profile>>(json_content);
                if (profiles_result.has_value()) {
                    saved_profiles_ = profiles_result.value();
                    TRELLO_INFO_FMT("Loaded {} saved profiles", saved_profiles_.size());
                }
            }
        }
    } catch (const std::exception& e) {
        TRELLO_ERROR_FMT("Failed to load saved profiles: {}", e.what());
    }
}

// JSON parsing helpers
trello_board trello_model::parse_board(const std::string& json_str) {
    trello_board board;
    
    auto error = glz::read<glz::opts{.error_on_unknown_keys=false}>(board, json_str);
    if (!error) {
        return board;
    }
    TRELLO_ERROR_FMT("Failed to parse board JSON: {}", glz::format_error(error, json_str));
    throw std::runtime_error("Failed to parse board JSON");
}

std::vector<trello_board> trello_model::parse_boards(const std::string& json_str) {
    std::vector<trello_board> boards;
    
    auto error = glz::read<glz::opts{.error_on_unknown_keys=false}>(boards, json_str);
    if (!error) {
        TRELLO_INFO_FMT("Successfully parsed {} boards", boards.size());
        return boards;
    }
    TRELLO_ERROR_FMT("Failed to parse boards JSON: {}", glz::format_error(error, json_str));
    throw std::runtime_error("Failed to parse boards JSON");
}

trello_card trello_model::parse_card(const std::string& json_str) {
    trello_card card;
    
    auto error = glz::read<glz::opts{.error_on_unknown_keys=false}>(card, json_str);
    if (!error) {
        return card;
    }
    TRELLO_ERROR_FMT("Failed to parse card JSON: {}", glz::format_error(error, json_str));
    throw std::runtime_error("Failed to parse card JSON");
}

std::vector<trello_card> trello_model::parse_cards(const std::string& json_str) {
    std::vector<trello_card> cards;
    
    auto error = glz::read<glz::opts{.error_on_unknown_keys=false}>(cards, json_str);
    if (!error) {
        TRELLO_INFO_FMT("Successfully parsed {} cards", cards.size());
        return cards;
    }
    TRELLO_ERROR_FMT("Failed to parse cards JSON: {}", glz::format_error(error, json_str));
    throw std::runtime_error("Failed to parse cards JSON");
}

trello_list trello_model::parse_list(const std::string& json_str) {
    trello_list list;
    auto error = glz::read<glz::opts{.error_on_unknown_keys=false}>(list, json_str);
    if (!error) {
        return list;
    }
    TRELLO_ERROR_FMT("Failed to parse list JSON: {}", glz::format_error(error, json_str));
    throw std::runtime_error("Failed to parse list JSON");
}

std::vector<trello_list> trello_model::parse_lists(const std::string& json_str) {
    std::vector<trello_list> lists;
    auto error = glz::read<glz::opts{.error_on_unknown_keys=false}>(lists, json_str);
    if (!error) {
        return lists;
    }
    TRELLO_ERROR_FMT("Failed to parse lists JSON: {}", glz::format_error(error, json_str));
    throw std::runtime_error("Failed to parse lists JSON");
}

std::vector<trello_member> trello_model::parse_members(const std::string& json_str) {
    std::vector<trello_member> members;
    auto error = glz::read<glz::opts{.error_on_unknown_keys=false}>(members, json_str);
    if (!error) {
        return members;
    }
    TRELLO_ERROR_FMT("Failed to parse members JSON: {}", glz::format_error(error, json_str));
    throw std::runtime_error("Failed to parse members JSON");
}

std::vector<trello_label> trello_model::parse_labels(const std::string& json_str) {
    std::vector<trello_label> labels;
    auto error = glz::read<glz::opts{.error_on_unknown_keys=false}>(labels, json_str);
    if (!error) {
        return labels;
    }
    TRELLO_ERROR_FMT("Failed to parse labels JSON: {}", glz::format_error(error, json_str));
    throw std::runtime_error("Failed to parse labels JSON");
}

// Missing method implementations

std::future<bool> trello_model::delete_card(const std::string& card_id) {
    return std::async(std::launch::async, [this, card_id]() -> bool { // NOLINT(bugprone-exception-escape)
        try {
            const std::string endpoint = "cards/" + card_id;
            make_request(endpoint, "DELETE");
            TRELLO_INFO_FMT("Deleted card: {}", card_id);
            return true;
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to delete card {}: {}", card_id, e.what());
            return false;
        }
    });
}

std::future<bool> trello_model::update_card(const std::string& card_id, const std::string& name, const std::string& desc) {
    return std::async(std::launch::async, [this, card_id, name, desc]() -> bool { // NOLINT(bugprone-exception-escape)
        try {
            const std::string endpoint = "cards/" + card_id;
            std::string params = "name=" + name;
            if (!desc.empty()) {
                params += "&desc=" + desc;
            }
            make_request(endpoint, "PUT", params);
            TRELLO_INFO_FMT("Updated card: {}", card_id);
            return true;
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to update card {}: {}", card_id, e.what());
            return false;
        }
    });
}

std::future<bool> trello_model::update_list(const std::string& list_id, const std::string& name) {
    return std::async(std::launch::async, [this, list_id, name]() -> bool { // NOLINT(bugprone-exception-escape)
        try {
            const std::string endpoint = "lists/" + list_id;
            const std::string params = "name=" + name;
            make_request(endpoint, "PUT", params);
            TRELLO_INFO_FMT("Updated list: {}", list_id);
            return true;
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to update list {}: {}", list_id, e.what());
            return false;
        }
    });
}

std::future<bool> trello_model::archive_list(const std::string& list_id) {
    return std::async(std::launch::async, [this, list_id]() -> bool { // NOLINT(bugprone-exception-escape)
        try {
            const std::string endpoint = "lists/" + list_id + "/closed";
            make_request(endpoint, "PUT", "value=true");
            TRELLO_INFO_FMT("Archived list: {}", list_id);
            return true;
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to archive list {}: {}", list_id, e.what());
            return false;
        }
    });
}

std::future<bool> trello_model::delete_board(const std::string& board_id) {
    return std::async(std::launch::async, [this, board_id]() -> bool { // NOLINT(bugprone-exception-escape)
        try {
            const std::string endpoint = "boards/" + board_id;
            make_request(endpoint, "DELETE");
            TRELLO_INFO_FMT("Deleted board: {}", board_id);
            return true;
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to delete board {}: {}", board_id, e.what());
            return false;
        }
    });
}

std::future<bool> trello_model::update_board(const std::string& board_id, const std::string& name, const std::string& desc) {
    return std::async(std::launch::async, [this, board_id, name, desc]() -> bool { // NOLINT(bugprone-exception-escape)
        try {
            const std::string endpoint = "boards/" + board_id;
            std::string params = "name=" + name;
            if (!desc.empty()) {
                params += "&desc=" + desc;
            }
            make_request(endpoint, "PUT", params);
            TRELLO_INFO_FMT("Updated board: {}", board_id);
            return true;
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to update board {}: {}", board_id, e.what());
            return false;
        }
    });
}

std::future<std::vector<trello_card>> trello_model::get_list_cards(const std::string& list_id) {
    return std::async(std::launch::async, [this, list_id]() -> std::vector<trello_card> { // NOLINT(bugprone-exception-escape)
        try {
            const std::string endpoint = "lists/" + list_id + "/cards";
            const std::string response = make_request(endpoint);
            return parse_cards(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to get cards for list {}: {}", list_id, e.what());
            return {};
        }
    });
}

std::future<trello_card> trello_model::get_card(const std::string& card_id) {
    return std::async(std::launch::async, [this, card_id]() -> trello_card { // NOLINT(bugprone-exception-escape)
        try {
            const std::string endpoint = "cards/" + card_id;
            const std::string response = make_request(endpoint);
            trello_card card;
            auto error = glz::read<glz::opts{.error_on_unknown_keys=false}>(card, response);
            if (!error) {
                return card;
            }
            TRELLO_ERROR_FMT("Failed to parse card JSON: {}", glz::format_error(error, response));
            throw std::runtime_error("Failed to parse card JSON");
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to get card {}: {}", card_id, e.what());
            throw;
        }
    });
}

} // namespace rouen::models::trello
