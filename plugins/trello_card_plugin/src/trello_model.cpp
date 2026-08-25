#include "trello_model.hpp"

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

#include "helpers/platform_utils.hpp"
#include "helpers/fetch.hpp"

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
        if (profile.name.empty()) {
            throw std::runtime_error("Profile name is empty");
        }
        if (profile.api_key.empty()) {
            throw std::runtime_error("API key is empty for profile '" + profile.name + "'");
        }
        if (profile.token.empty()) {
            throw std::runtime_error("Token is empty for profile '" + profile.name + "'");
        }
        
        std::string response = test_connection_request("members/me");
        
        if (response.empty()) {
            throw std::runtime_error("Empty response from Trello API");
        }
        
        auto json_result = glz::read_json<glz::json_t>(response);
        if (!json_result.has_value()) {
            throw std::runtime_error("Failed to parse JSON response from Trello API");
        }
        
        connected_ = true;
        TRELLO_INFO("Successfully connected to Trello:");
        TRELLO_INFO_FMT("  Profile: {}", profile.name);
        TRELLO_INFO_FMT("  API Key: {}...", profile.api_key.substr(0, 8));
        
        if (!profile.is_environment) {
            std::lock_guard<std::mutex> const lock(profiles_mutex_);
            auto it = std::find_if(saved_profiles_.begin(), saved_profiles_.end(),
                                 [&profile](const auto& p) { return p.name == profile.name; });
            if (it == saved_profiles_.end()) {
                saved_profiles_.push_back(profile);
                std::string profiles_json;
                auto write_result = glz::write_json(saved_profiles_, profiles_json);
                if (!write_result) {
                    std::ofstream file(get_profiles_path());
                    file << profiles_json;
                }
            }
        }
    } catch (const std::exception& e) {
        connected_ = false;
        TRELLO_ERROR_FMT("Failed to connect to Trello: {}", e.what());
        throw;
    }
    
    return connected_;
}

bool trello_model::connect_from_environment() {
    auto api_key = rouen::platform::get_env("TRELLO_API_KEY");
    auto token = rouen::platform::get_env("TRELLO_TOKEN");
    
    if (api_key.empty() || token.empty()) {
        TRELLO_DEBUG("Trello environment variables not found");
        return false;
    }
    
    trello_connection_profile profile;
    profile.name = "Environment";
    profile.api_key = api_key;
    profile.token = token;
    profile.is_environment = true;
    
    try {
        return connect(profile);
    } catch (const std::exception& e) {
        TRELLO_ERROR_FMT("Failed to connect with environment credentials: {}", e.what());
        return false;
    }
}

void trello_model::disconnect() {
    connected_ = false;
    current_profile_ = trello_connection_profile();
    TRELLO_INFO("Disconnected from Trello");
}

std::vector<trello_connection_profile> trello_model::get_saved_profiles() const {
    std::lock_guard<std::mutex> const lock(profiles_mutex_);
    return saved_profiles_;
}

void trello_model::save_profile(const trello_connection_profile& profile) {
    std::lock_guard<std::mutex> const lock(profiles_mutex_);
    
    auto it = std::find_if(saved_profiles_.begin(), saved_profiles_.end(),
                         [&profile](const auto& p) { return p.name == profile.name; });
                         
    if (it != saved_profiles_.end()) {
        *it = profile;
    } else {
        saved_profiles_.push_back(profile);
    }
    
    std::string profiles_json;
    auto write_result = glz::write_json(saved_profiles_, profiles_json);
    if (!write_result) {
        std::ofstream file(get_profiles_path());
        file << profiles_json;
    }
}

void trello_model::delete_profile(const std::string& profile_name) {
    std::lock_guard<std::mutex> const lock(profiles_mutex_);
    
    saved_profiles_.erase(
        std::remove_if(saved_profiles_.begin(), saved_profiles_.end(),
                       [&profile_name](const auto& p) { return p.name == profile_name; }),
        saved_profiles_.end()
    );
    
    std::string profiles_json;
    auto write_result = glz::write_json(saved_profiles_, profiles_json);
    if (!write_result) {
        std::ofstream file(get_profiles_path());
        file << profiles_json;
    }
}

std::string trello_model::make_request(const std::string& endpoint, const std::string& method, const std::string& data) {
    if (!connected_) {
        throw std::runtime_error("Not connected to Trello");
    }
    
    std::string const url = build_url(endpoint);
    TRELLO_DEBUG_FMT("Making Trello request: {} {}", method, url);
    
    http::fetch fetcher;
    std::string response;
    
    std::vector<std::string> const form_headers = {"Content-Type: application/x-www-form-urlencoded"};

    if (method == "GET") {
        response = fetcher(url);
    } else if (method == "POST") {
        response = fetcher.post(url, data, form_headers);
    } else if (method == "PUT") {
        response = fetcher.put(url, data, form_headers);
    } else if (method == "DELETE") {
        std::vector<std::string> const delete_headers = {"Content-Type: application/x-www-form-urlencoded", "X-HTTP-Method-Override: DELETE"};
        response = fetcher.post(url, data, delete_headers);
    } else {
        throw std::runtime_error("Unsupported HTTP method: " + method);
    }
    
    return response;
}

std::string trello_model::test_connection_request(const std::string& endpoint, const std::string& /* method */, const std::string& /* data */) {
    std::string const url = build_url(endpoint);
    TRELLO_DEBUG_FMT("Testing Trello connection to {}", url);
    
    http::fetch fetcher;
    return fetcher(url);
}

std::string trello_model::build_url(const std::string& endpoint) const {
    std::string base_url = "https://api.trello.com/1/";
    std::string url = base_url + endpoint;
    
    std::string const separator = (endpoint.find('?') != std::string::npos) ? "&" : "?";
    url += separator + "key=" + current_profile_.api_key + "&token=" + current_profile_.token;
    
    return url;
}

std::filesystem::path trello_model::get_profiles_path() {
    return rouen::platform::get_user_data_path("trello_profiles.json", true);
}

std::filesystem::path trello_model::get_profiles_file_path() {
    return get_profiles_path();
}

void trello_model::load_saved_profiles() {
    auto profiles_path = get_profiles_path();
    if (std::filesystem::exists(profiles_path)) {
        try {
            std::ifstream file(profiles_path);
            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            auto error = glz::read_json(saved_profiles_, content);
            if (error) {
                TRELLO_ERROR("Failed to parse saved profiles");
            }
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Error reading saved profiles file: {}", e.what());
        }
    }
}

trello_board trello_model::parse_board(const std::string& json_str) {
    trello_board board;
    auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(board, json_str);
    if (error) {
        TRELLO_ERROR_FMT("Failed to parse board JSON: {}", glz::format_error(error, json_str));
        throw std::runtime_error("Failed to parse board JSON");
    }
    return board;
}

std::vector<trello_board> trello_model::parse_boards(const std::string& json_str) {
    std::vector<trello_board> boards;
    auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(boards, json_str);
    if (error) {
        TRELLO_ERROR_FMT("Failed to parse boards JSON: {}", glz::format_error(error, json_str));
        throw std::runtime_error("Failed to parse boards JSON");
    }
    return boards;
}

trello_card trello_model::parse_card(const std::string& json_str) {
    trello_card card;
    auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(card, json_str);
    if (error) {
        TRELLO_ERROR_FMT("Failed to parse card JSON: {}", glz::format_error(error, json_str));
        throw std::runtime_error("Failed to parse card JSON");
    }
    return card;
}

std::vector<trello_card> trello_model::parse_cards(const std::string& json_str) {
    std::vector<trello_card> cards;
    auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(cards, json_str);
    if (error) {
        TRELLO_ERROR_FMT("Failed to parse cards JSON: {}", glz::format_error(error, json_str));
        throw std::runtime_error("Failed to parse cards JSON");
    }
    return cards;
}

trello_list trello_model::parse_list(const std::string& json_str) {
    trello_list list;
    auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(list, json_str);
    if (error) {
        TRELLO_ERROR_FMT("Failed to parse list JSON: {}", glz::format_error(error, json_str));
        throw std::runtime_error("Failed to parse list JSON");
    }
    return list;
}

std::vector<trello_list> trello_model::parse_lists(const std::string& json_str) {
    std::vector<trello_list> lists;
    auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(lists, json_str);
    if (error) {
        TRELLO_ERROR_FMT("Failed to parse lists JSON: {}", glz::format_error(error, json_str));
        throw std::runtime_error("Failed to parse lists JSON");
    }
    return lists;
}

std::vector<trello_member> trello_model::parse_members(const std::string& json_str) {
    std::vector<trello_member> members;
    auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(members, json_str);
    if (error) {
        TRELLO_ERROR_FMT("Failed to parse members JSON: {}", glz::format_error(error, json_str));
        throw std::runtime_error("Failed to parse members JSON");
    }
    return members;
}

std::vector<trello_label> trello_model::parse_labels(const std::string& json_str) {
    std::vector<trello_label> labels;
    auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(labels, json_str);
    if (error) {
        TRELLO_ERROR_FMT("Failed to parse labels JSON: {}", glz::format_error(error, json_str));
        throw std::runtime_error("Failed to parse labels JSON");
    }
    return labels;
}

std::future<std::vector<trello_board>> trello_model::get_user_boards() {
    return std::async(std::launch::async, [this]() -> std::vector<trello_board> {
        try {
            const std::string endpoint = "members/me/boards?lists=open&cards=open";
            const std::string response = make_request(endpoint);
            return parse_boards(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to get user boards: {}", e.what());
            return {};
        }
    });
}

std::future<trello_board> trello_model::get_board(const std::string& board_id, bool include_lists, bool include_cards) {
    return std::async(std::launch::async, [this, board_id, include_lists, include_cards]() -> trello_board {
        try {
            std::string endpoint = "boards/" + board_id;
            std::vector<std::string> params;
            if (include_lists) params.push_back("lists=open");
            if (include_cards) params.push_back("cards=open");
            if (!params.empty()) {
                endpoint += "?";
                for (size_t i = 0; i < params.size(); ++i) {
                    if (i > 0) endpoint += "&";
                    endpoint += params[i];
                }
            }
            
            const std::string response = make_request(endpoint);
            return parse_board(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to get board {}: {}", board_id, e.what());
            throw;
        }
    });
}

std::future<bool> trello_model::create_board(const std::string& name, const std::string& desc) {
    return std::async(std::launch::async, [this, name, desc]() -> bool {
        try {
            const std::string endpoint = "boards";
            std::string params = "name=" + name;
            if (!desc.empty()) {
                params += "&desc=" + desc;
            }
            make_request(endpoint, "POST", params);
            TRELLO_INFO_FMT("Created board: {}", name);
            return true;
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to create board {}: {}", name, e.what());
            return false;
        }
    });
}

std::future<std::vector<trello_list>> trello_model::get_board_lists(const std::string& board_id) {
    return std::async(std::launch::async, [this, board_id]() -> std::vector<trello_list> {
        try {
            const std::string endpoint = "boards/" + board_id + "/lists";
            const std::string response = make_request(endpoint);
            return parse_lists(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to get lists for board {}: {}", board_id, e.what());
            return {};
        }
    });
}

std::future<trello_list> trello_model::create_list(const std::string& board_id, const std::string& name, float pos) {
    return std::async(std::launch::async, [this, board_id, name, pos]() -> trello_list {
        try {
            const std::string endpoint = "lists";
            std::string params = "idBoard=" + board_id + "&name=" + name;
            if (pos > 0.0f) {
                params += "&pos=" + std::to_string(pos);
            }
            const std::string response = make_request(endpoint, "POST", params);
            TRELLO_INFO_FMT("Created list: {} on board {}", name, board_id);
            return parse_list(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to create list {} on board {}: {}", name, board_id, e.what());
            throw;
        }
    });
}

std::future<std::vector<trello_card>> trello_model::get_board_cards(const std::string& board_id) {
    return std::async(std::launch::async, [this, board_id]() -> std::vector<trello_card> {
        try {
            const std::string endpoint = "boards/" + board_id + "/cards";
            const std::string response = make_request(endpoint);
            return parse_cards(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to get cards for board {}: {}", board_id, e.what());
            return {};
        }
    });
}

std::future<trello_card> trello_model::create_card(const std::string& list_id, const std::string& name, const std::string& desc, float pos) {
    return std::async(std::launch::async, [this, list_id, name, desc, pos]() -> trello_card {
        try {
            const std::string endpoint = "cards";
            std::string params = "idList=" + list_id + "&name=" + name;
            if (!desc.empty()) {
                params += "&desc=" + desc;
            }
            if (pos > 0.0f) {
                params += "&pos=" + std::to_string(pos);
            }
            const std::string response = make_request(endpoint, "POST", params);
            TRELLO_INFO_FMT("Created card: {} on list {}", name, list_id);
            return parse_card(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to create card {} on list {}: {}", name, list_id, e.what());
            throw;
        }
    });
}

std::future<bool> trello_model::update_card(const std::string& card_id, const std::string& name, const std::string& desc) {
    return std::async(std::launch::async, [this, card_id, name, desc]() -> bool {
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

std::future<bool> trello_model::move_card(const std::string& card_id, const std::string& list_id, float pos) {
    return std::async(std::launch::async, [this, card_id, list_id, pos]() -> bool {
        try {
            const std::string endpoint = "cards/" + card_id;
            std::string params = "idList=" + list_id;
            if (pos > 0.0f) {
                params += "&pos=" + std::to_string(pos);
            }
            make_request(endpoint, "PUT", params);
            TRELLO_INFO_FMT("Moved card {} to list {}", card_id, list_id);
            return true;
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to move card {} to list {}: {}", card_id, list_id, e.what());
            return false;
        }
    });
}

std::future<bool> trello_model::delete_card(const std::string& card_id) {
    return std::async(std::launch::async, [this, card_id]() -> bool {
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

std::future<std::vector<trello_member>> trello_model::get_board_members(const std::string& board_id) {
    return std::async(std::launch::async, [this, board_id]() -> std::vector<trello_member> {
        try {
            const std::string endpoint = "boards/" + board_id + "/members";
            const std::string response = make_request(endpoint);
            return parse_members(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to get members for board {}: {}", board_id, e.what());
            return {};
        }
    });
}

std::future<std::vector<trello_label>> trello_model::get_board_labels(const std::string& board_id) {
    return std::async(std::launch::async, [this, board_id]() -> std::vector<trello_label> {
        try {
            const std::string endpoint = "boards/" + board_id + "/labels";
            const std::string response = make_request(endpoint);
            return parse_labels(response);
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to get labels for board {}: {}", board_id, e.what());
            return {};
        }
    });
}

std::future<std::vector<trello_card>> trello_model::search_cards(const std::string& query, const std::string& board_id) {
    return std::async(std::launch::async, [this, query, board_id]() -> std::vector<trello_card> {
        try {
            std::string endpoint = "search?query=" + query + "&modelTypes=cards";
            if (!board_id.empty()) {
                endpoint += "&idBoards=" + board_id;
            }
            const std::string response = make_request(endpoint);
            
            auto json_result = glz::read_json<glz::json_t>(response);
            if (json_result.has_value() && json_result.value().contains("cards")) {
                std::string cards_json;
                auto result = glz::write_json(json_result.value()["cards"], cards_json);
                if (!result) {
                    return parse_cards(cards_json);
                }
            }
            return {};
        } catch (const std::exception& e) {
            TRELLO_ERROR_FMT("Failed to search cards with query '{}': {}", query, e.what());
            return {};
        }
    });
}

std::future<bool> trello_model::update_list(const std::string& list_id, const std::string& name) {
    return std::async(std::launch::async, [this, list_id, name]() -> bool {
        try {
            const std::string endpoint = "lists/" + list_id;
            std::string params = "name=" + name;
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
    return std::async(std::launch::async, [this, list_id]() -> bool {
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
    return std::async(std::launch::async, [this, board_id]() -> bool {
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
    return std::async(std::launch::async, [this, board_id, name, desc]() -> bool {
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
    return std::async(std::launch::async, [this, list_id]() -> std::vector<trello_card> {
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
    return std::async(std::launch::async, [this, card_id]() -> trello_card {
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
