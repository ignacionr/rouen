#pragma once

#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <memory>
#include <mutex>
#include "sqlite.hpp"

namespace rouen::helpers {

class tag_manager {
public:
    static tag_manager& get();

    // Associate a tag with a URI (and optional display title)
    void add_tag(const std::string& uri, const std::string& tag, const std::string& title = "");

    // Set a friendly display title for a URI
    void set_uri_title(const std::string& uri, const std::string& title);

    // Get the display title for a URI
    std::string get_uri_title(const std::string& uri);

    // Dissociate a tag from a URI
    void remove_tag(const std::string& uri, const std::string& tag);

    // Dissociate all tags from a URI
    void remove_all_tags(const std::string& uri);

    // Get all tags associated with a specific URI
    std::set<std::string> get_tags(const std::string& uri);

    // Check if a URI has a specific tag
    bool has_tag(const std::string& uri, const std::string& tag);

    // Get all unique tags defined in the system, sorted by sort_order then alphabetically
    std::vector<std::string> get_available_tags();

    // Get all URIs associated with a specific tag
    std::vector<std::string> get_uris_by_tag(const std::string& tag);

    // Get tag counts: map tag -> number of URIs associated with it
    std::unordered_map<std::string, int> get_tag_counts();

    // Ensure a tag definition exists in the database
    void ensure_tag_defined(const std::string& tag, int sort_order = 999);

    // Delete all tag definitions that are not associated with any URI
    int delete_unused_tags();

private:
    tag_manager();
    ~tag_manager() = default;

    tag_manager(const tag_manager&) = delete;
    tag_manager& operator=(const tag_manager&) = delete;

    std::unique_ptr<hosting::db::sqlite> db_;
    std::mutex mutex_;
};

} // namespace rouen::helpers
