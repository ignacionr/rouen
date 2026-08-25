#pragma once

#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <glaze/glaze.hpp>

#include "helpers/api_keys.hpp"
#include "helpers/platform_utils.hpp"
#include "helpers/fetch.hpp"
#include "helpers/debug.hpp"

#define TRELLO_ERROR(message) LOG_COMPONENT("TRELLO", LOG_LEVEL_ERROR, message)
#define TRELLO_WARN(message) LOG_COMPONENT("TRELLO", LOG_LEVEL_WARN, message)
#define TRELLO_INFO(message) LOG_COMPONENT("TRELLO", LOG_LEVEL_INFO, message)
#define TRELLO_DEBUG(message) LOG_COMPONENT("TRELLO", LOG_LEVEL_DEBUG, message)
#define TRELLO_TRACE(message) LOG_COMPONENT("TRELLO", LOG_LEVEL_TRACE, message)

#define TRELLO_ERROR_FMT(fmt, ...) TRELLO_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define TRELLO_WARN_FMT(fmt, ...) TRELLO_WARN(debug::format_log(fmt, __VA_ARGS__))
#define TRELLO_INFO_FMT(fmt, ...) TRELLO_INFO(debug::format_log(fmt, __VA_ARGS__))
#define TRELLO_DEBUG_FMT(fmt, ...) TRELLO_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define TRELLO_TRACE_FMT(fmt, ...) TRELLO_TRACE(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::models::trello {

struct trello_connection_profile {
    std::string name;
    std::string api_key;
    std::string api_secret;
    std::string token;
    bool is_environment = false;
    
    bool is_valid() const {
        return !api_key.empty() && !token.empty();
    }
};

struct trello_label {
    std::string id;
    std::string name;
    std::string color;
};

struct trello_member {
    std::string id;
    std::string username;
    std::string full_name;
    std::optional<std::string> avatar_url;
    std::optional<std::string> avatar_hash;
    std::optional<std::string> initials;
    std::optional<std::string> idMemberReferrer;
    bool activityBlocked = false;
    bool nonPublicAvailable = true;
};

struct trello_badges {
    int votes = 0;
    int comments = 0;
    int attachments = 0;
    int checkItems = 0;
    int checkItemsChecked = 0;
    std::optional<std::string> checkItemsEarliestDue;
    bool subscribed = false;
    std::string fogbugz;
    std::optional<std::string> due;
    bool description = false;
    bool location = false;
};

struct trello_list {
    std::string id;
    std::string name;
    std::string idBoard;
    bool closed = false;
    float pos = 0.0f;
    bool subscribed = false;
    std::optional<std::string> color;
    std::optional<int> softLimit;
    std::optional<std::string> type;
};

struct trello_card {
    std::string id;
    std::string name;
    std::string desc;
    std::string idList;
    std::string idBoard;
    std::optional<std::string> due;
    bool dueComplete = false;
    bool closed = false;
    std::string url;
    std::string shortUrl;
    std::string shortLink;
    std::string nodeId;
    std::optional<std::string> lastUpdatedBy;
    float pos = 0.0f;
    std::vector<std::string> idLabels;
    std::vector<std::string> idMembers;
    std::vector<std::string> idChecklists;
    trello_badges badges;
    
    int badges_votes() const { return badges.votes; }
    int badges_comments() const { return badges.comments; }
    int badges_attachments() const { return badges.attachments; }
    int badges_checkitems() const { return badges.checkItems; }
    int badges_checkitems_checked() const { return badges.checkItemsChecked; }
};

struct trello_board {
    std::string id;
    std::string name;
    std::string desc;
    bool closed = false;
    bool starred = false;
    std::string url;
    std::string shortUrl;
    std::string shortLink;
    std::optional<std::string> idOrganization;
    std::optional<std::string> idEnterprise;
    std::string nodeId;
    std::vector<trello_list> lists;
    std::vector<trello_card> cards;
    std::vector<trello_label> labels;
    std::vector<trello_member> members;
    
    const trello_list* get_list(const std::string& list_id) const {
        for (const auto& list : lists) {
            if (list.id == list_id) return &list;
        }
        return nullptr;
    }
    
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

struct trello_organization {
    std::string id;
    std::string name;
    std::string display_name;
    std::string desc;
    std::string url;
};

class trello_model {
public:
    trello_model();
    ~trello_model() = default;

    bool connect(const trello_connection_profile& profile);
    bool connect_from_environment();
    void disconnect();
    bool is_connected() const { return connected_; }
    
    std::vector<trello_connection_profile> get_saved_profiles() const;
    void save_profile(const trello_connection_profile& profile);
    void delete_profile(const std::string& profile_name);
    trello_connection_profile get_current_profile() const { return current_profile_; }
    
    std::future<std::vector<trello_board>> get_user_boards();
    std::future<trello_board> get_board(const std::string& board_id, bool include_lists = true, bool include_cards = true);
    std::future<bool> create_board(const std::string& name, const std::string& desc = "");
    std::future<bool> update_board(const std::string& board_id, const std::string& name, const std::string& desc = "");
    std::future<bool> delete_board(const std::string& board_id);
    
    std::future<std::vector<trello_list>> get_board_lists(const std::string& board_id);
    std::future<trello_list> create_list(const std::string& board_id, const std::string& name, float pos = 0.0f);
    std::future<bool> update_list(const std::string& list_id, const std::string& name);
    std::future<bool> archive_list(const std::string& list_id);
    
    std::future<std::vector<trello_card>> get_board_cards(const std::string& board_id);
    std::future<std::vector<trello_card>> get_list_cards(const std::string& list_id);
    std::future<trello_card> get_card(const std::string& card_id);
    std::future<trello_card> create_card(const std::string& list_id, const std::string& name, const std::string& desc = "", float pos = 0.0f);
    std::future<bool> update_card(const std::string& card_id, const std::string& name, const std::string& desc = "");
    std::future<bool> move_card(const std::string& card_id, const std::string& list_id, float pos = 0.0f);
    std::future<bool> delete_card(const std::string& card_id);
    
    std::future<std::vector<trello_member>> get_board_members(const std::string& board_id);
    std::future<std::vector<trello_label>> get_board_labels(const std::string& board_id);
    
    std::future<std::vector<trello_card>> search_cards(const std::string& query, const std::string& board_id = "");
    
    static std::filesystem::path get_profiles_file_path();

private:
    bool connected_ = false;
    trello_connection_profile current_profile_;
    mutable std::mutex profiles_mutex_;
    
    std::string make_request(const std::string& endpoint, const std::string& method = "GET", const std::string& data = "");
    std::string test_connection_request(const std::string& endpoint, const std::string& method = "GET", const std::string& data = "");
    std::string build_url(const std::string& endpoint) const;
    
    static std::filesystem::path get_profiles_path();
    void load_saved_profiles();
    std::vector<trello_connection_profile> saved_profiles_;
    
    static trello_board parse_board(const std::string& json_str);
    static std::vector<trello_board> parse_boards(const std::string& json_str);
    static trello_card parse_card(const std::string& json_str);
    static std::vector<trello_card> parse_cards(const std::string& json_str);
    static trello_list parse_list(const std::string& json_str);
    static std::vector<trello_list> parse_lists(const std::string& json_str);
    static std::vector<trello_member> parse_members(const std::string& json_str);
    static std::vector<trello_label> parse_labels(const std::string& json_str);
};

std::shared_ptr<trello_model> get_trello_model();

} // namespace rouen::models::trello

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
