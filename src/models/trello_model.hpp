#pragma once

// 1. Standard includes in alphabetic order
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
#include <glaze/glaze.hpp>

// 3. All other includes
#include "../helpers/api_keys.hpp"
#include "../helpers/config_service.hpp"
#include "../helpers/fetch.hpp"
#include "../helpers/debug.hpp"

// Define Trello-specific logging macros
#define TRELLO_ERROR(message) LOG_COMPONENT("TRELLO", LOG_LEVEL_ERROR, message)
#define TRELLO_WARN(message) LOG_COMPONENT("TRELLO", LOG_LEVEL_WARN, message)
#define TRELLO_INFO(message) LOG_COMPONENT("TRELLO", LOG_LEVEL_INFO, message)
#define TRELLO_DEBUG(message) LOG_COMPONENT("TRELLO", LOG_LEVEL_DEBUG, message)
#define TRELLO_TRACE(message) LOG_COMPONENT("TRELLO", LOG_LEVEL_TRACE, message)

// Format-enabled macros
#define TRELLO_ERROR_FMT(fmt, ...) TRELLO_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define TRELLO_WARN_FMT(fmt, ...) TRELLO_WARN(debug::format_log(fmt, __VA_ARGS__))
#define TRELLO_INFO_FMT(fmt, ...) TRELLO_INFO(debug::format_log(fmt, __VA_ARGS__))
#define TRELLO_DEBUG_FMT(fmt, ...) TRELLO_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define TRELLO_TRACE_FMT(fmt, ...) TRELLO_TRACE(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::models::trello {

// Trello connection profile for authentication
struct trello_connection_profile {
    std::string name;
    std::string api_key;
    std::string api_secret;  // For OAuth and webhooks
    std::string token;       // User token
    bool is_environment = false;  // Loaded from environment variables
    
    bool is_valid() const {
        return !api_key.empty() && !token.empty();
    }
};

// Trello label structure
struct trello_label {
    std::string id;
    std::string name;
    std::string color;
};

// Trello member structure
struct trello_member {
    std::string id;
    std::string username;
    std::string full_name;
    std::optional<std::string> avatar_url;  // Can be null
    std::optional<std::string> avatar_hash;  // Can be null
    std::optional<std::string> initials;
    std::optional<std::string> idMemberReferrer;  // Can be null
    bool activityBlocked = false;
    bool nonPublicAvailable = true;
};

// Trello badges structure (nested in cards)
struct trello_badges {
    int votes = 0;
    int comments = 0;
    int attachments = 0;
    int checkItems = 0;
    int checkItemsChecked = 0;
    std::optional<std::string> checkItemsEarliestDue;  // Can be null
    bool subscribed = false;
    std::string fogbugz;  // Empty string when not used
    std::optional<std::string> due;  // Can be null
    bool description = false;  // Boolean indicating if description exists
    bool location = false;  // Boolean indicating if location exists
};

// Trello list structure
struct trello_list {
    std::string id;
    std::string name;
    std::string idBoard;  // Actual field name in Trello API
    bool closed = false;
    float pos = 0.0f;  // Position for ordering
    bool subscribed = false;
    std::optional<std::string> color;  // Can be null
    std::optional<int> softLimit;  // Can be null
    std::optional<std::string> type;  // Can be null
    // Note: datasource is ignored as it's complex and not typically used
};

// Trello card structure
struct trello_card {
    std::string id;
    std::string name;
    std::string desc;
    std::string idList;   // Actual field name
    std::string idBoard;  // Actual field name
    std::optional<std::string> due;  // ISO date string, can be null
    bool dueComplete = false;
    bool closed = false;
    std::string url;
    std::string shortUrl;  // Actual field name
    std::string shortLink; // Additional field
    std::string nodeId;    // New field
    std::optional<std::string> lastUpdatedBy;  // Can be null
    float pos = 0.0f;  // Position for ordering
    std::vector<std::string> idLabels;     // Actual field name
    std::vector<std::string> idMembers;    // Actual field name
    std::vector<std::string> idChecklists; // Actual field name
    trello_badges badges;  // Nested badges structure
    
    // Convenience accessors for backward compatibility
    int badges_votes() const { return badges.votes; }
    int badges_comments() const { return badges.comments; }
    int badges_attachments() const { return badges.attachments; }
    int badges_checkitems() const { return badges.checkItems; }
    int badges_checkitems_checked() const { return badges.checkItemsChecked; }
};

// Trello board structure
struct trello_board {
    std::string id;
    std::string name;
    std::string desc;
    bool closed = false;
    bool starred = false;
    std::string url;
    std::string shortUrl;  // Actual field name
    std::string shortLink; // Additional field
    std::optional<std::string> idOrganization;  // Can be null
    std::optional<std::string> idEnterprise;    // Can be null
    std::string nodeId;  // New field that was causing issues
    std::vector<trello_list> lists;
    std::vector<trello_card> cards;
    std::vector<trello_label> labels;
    std::vector<trello_member> members;
    
    // Get list by ID
    const trello_list* get_list(const std::string& list_id) const {
        for (const auto& list : lists) {
            if (list.id == list_id) return &list;
        }
        return nullptr;
    }
    
    // Get cards for a specific list
    std::vector<trello_card> get_cards_for_list(const std::string& list_id) const {
        std::vector<trello_card> result;
        for (const auto& card : cards) {
            if (card.idList == list_id && !card.closed) {
                result.push_back(card);
            }
        }
        return result;
    }
};

// Trello organization structure
struct trello_organization {
    std::string id;
    std::string name;
    std::string display_name;
    std::string desc;
    std::string url;
};

// Main Trello model class for API management
class trello_model {
public:
    trello_model();
    ~trello_model() = default;

    // Connection management
    bool connect(const trello_connection_profile& profile);
    bool connect_from_environment();
    void disconnect();
    bool is_connected() const { return connected_; }
    
    // Profile management
    std::vector<trello_connection_profile> get_saved_profiles() const;
    void save_profile(const trello_connection_profile& profile);
    void delete_profile(const std::string& profile_name);
    trello_connection_profile get_current_profile() const { return current_profile_; }
    
    // Async API operations - Boards
    std::future<std::vector<trello_board>> get_user_boards();
    std::future<trello_board> get_board(const std::string& board_id, bool include_lists = true, bool include_cards = true);
    std::future<bool> create_board(const std::string& name, const std::string& desc = "");
    std::future<bool> update_board(const std::string& board_id, const std::string& name, const std::string& desc = "");
    std::future<bool> delete_board(const std::string& board_id);
    
    // Async API operations - Lists
    std::future<std::vector<trello_list>> get_board_lists(const std::string& board_id);
    std::future<trello_list> create_list(const std::string& board_id, const std::string& name, float pos = 0.0f);
    std::future<bool> update_list(const std::string& list_id, const std::string& name);
    std::future<bool> archive_list(const std::string& list_id);
    
    // Async API operations - Cards
    std::future<std::vector<trello_card>> get_board_cards(const std::string& board_id);
    std::future<std::vector<trello_card>> get_list_cards(const std::string& list_id);
    std::future<trello_card> get_card(const std::string& card_id);
    std::future<trello_card> create_card(const std::string& list_id, const std::string& name, const std::string& desc = "", float pos = 0.0f);
    std::future<bool> update_card(const std::string& card_id, const std::string& name, const std::string& desc = "");
    std::future<bool> move_card(const std::string& card_id, const std::string& list_id, float pos = 0.0f);
    std::future<bool> delete_card(const std::string& card_id);
    
    // Async API operations - Members and Labels
    std::future<std::vector<trello_member>> get_board_members(const std::string& board_id);
    std::future<std::vector<trello_label>> get_board_labels(const std::string& board_id);
    
    // Search functionality
    std::future<std::vector<trello_card>> search_cards(const std::string& query, const std::string& board_id = "");
    
    // Helper method to get the profiles file path (public to allow external utilities)
    static std::filesystem::path get_profiles_file_path();

private:
    // Connection state
    bool connected_ = false;
    trello_connection_profile current_profile_;
    mutable std::mutex profiles_mutex_;
    
    // Internal API request methods
    std::string make_request(const std::string& endpoint, const std::string& method = "GET", const std::string& data = "");
    std::string test_connection_request(const std::string& endpoint, const std::string& method = "GET", const std::string& data = "");
    std::string build_url(const std::string& endpoint) const;
    
    // Profile persistence
    static std::filesystem::path get_profiles_path();
    void load_saved_profiles();
    std::vector<trello_connection_profile> saved_profiles_;
    
    // JSON parsing helpers
    static trello_board parse_board(const std::string& json_str);
    static std::vector<trello_board> parse_boards(const std::string& json_str);
    static trello_card parse_card(const std::string& json_str);
    static std::vector<trello_card> parse_cards(const std::string& json_str);
    static trello_list parse_list(const std::string& json_str);
    static std::vector<trello_list> parse_lists(const std::string& json_str);
    static std::vector<trello_member> parse_members(const std::string& json_str);
    static std::vector<trello_label> parse_labels(const std::string& json_str);
};

// Global instance accessor (singleton pattern like JIRA)
std::shared_ptr<trello_model> get_trello_model();

} // namespace rouen::models::trello

// GLZ JSON mappings for serialization
template<>
struct glz::meta<rouen::models::trello::trello_connection_profile> {
    using T = rouen::models::trello::trello_connection_profile;
    static constexpr auto value = object(
        "name", &T::name,
        "api_key", &T::api_key,
        "api_secret", &T::api_secret,
        "token", &T::token,
        "is_environment", &T::is_environment
    );
};

template<>
struct glz::meta<rouen::models::trello::trello_label> {
    using T = rouen::models::trello::trello_label;
    static constexpr auto value = object(
        "id", &T::id,
        "name", &T::name,
        "color", &T::color
    );
};

template<>
struct glz::meta<rouen::models::trello::trello_member> {
    using T = rouen::models::trello::trello_member;
    static constexpr auto value = object(
        "id", &T::id,
        "username", &T::username,
        "fullName", &T::full_name,
        "avatarUrl", &T::avatar_url,
        "avatarHash", &T::avatar_hash,
        "initials", &T::initials,
        "idMemberReferrer", &T::idMemberReferrer,
        "activityBlocked", &T::activityBlocked,
        "nonPublicAvailable", &T::nonPublicAvailable
    );
};

template<>
struct glz::meta<rouen::models::trello::trello_badges> {
    using T = rouen::models::trello::trello_badges;
    static constexpr auto value = object(
        "votes", &T::votes,
        "comments", &T::comments,
        "attachments", &T::attachments,
        "checkItems", &T::checkItems,
        "checkItemsChecked", &T::checkItemsChecked,
        "checkItemsEarliestDue", &T::checkItemsEarliestDue,
        "subscribed", &T::subscribed,
        "fogbugz", &T::fogbugz,
        "due", &T::due,
        "description", &T::description,
        "location", &T::location
    );
};

template<>
struct glz::meta<rouen::models::trello::trello_list> {
    using T = rouen::models::trello::trello_list;
    static constexpr auto value = object(
        "id", &T::id,
        "name", &T::name,
        "idBoard", &T::idBoard,
        "closed", &T::closed,
        "pos", &T::pos,
        "subscribed", &T::subscribed,
        "color", &T::color,
        "softLimit", &T::softLimit,
        "type", &T::type
    );
};

template<>
struct glz::meta<rouen::models::trello::trello_card> {
    using T = rouen::models::trello::trello_card;
    static constexpr auto value = object(
        "id", &T::id,
        "name", &T::name,
        "desc", &T::desc,
        "idList", &T::idList,
        "idBoard", &T::idBoard,
        "due", &T::due,
        "dueComplete", &T::dueComplete,
        "closed", &T::closed,
        "url", &T::url,
        "shortUrl", &T::shortUrl,
        "shortLink", &T::shortLink,
        "nodeId", &T::nodeId,
        "lastUpdatedBy", &T::lastUpdatedBy,
        "pos", &T::pos,
        "idLabels", &T::idLabels,
        "idMembers", &T::idMembers,
        "idChecklists", &T::idChecklists,
        "badges", &T::badges
    );
};

template<>
struct glz::meta<rouen::models::trello::trello_board> {
    using T = rouen::models::trello::trello_board;
    static constexpr auto value = object(
        "id", &T::id,
        "name", &T::name,
        "desc", &T::desc,
        "closed", &T::closed,
        "starred", &T::starred,
        "url", &T::url,
        "shortUrl", &T::shortUrl,
        "shortLink", &T::shortLink,
        "nodeId", &T::nodeId,
        "idOrganization", &T::idOrganization,
        "lists", &T::lists,
        "cards", &T::cards,
        "labels", &T::labels,
        "members", &T::members
    );
};
