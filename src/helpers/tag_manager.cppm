module;

#include <concepts>
#include <utility>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <memory>
#include <mutex>
export module rouen.helpers.tag_manager;

namespace hosting::db {
    struct sqlite;
}

export namespace rouen::helpers {

class tag_manager {
public:
    static tag_manager& get();

    void set_database_path(const std::string& db_path);
    void add_tag(const std::string& uri, const std::string& tag, const std::string& title = "");
    void set_uri_title(const std::string& uri, const std::string& title);
    std::string get_uri_title(const std::string& uri);
    void remove_tag(const std::string& uri, const std::string& tag);
    void remove_all_tags(const std::string& uri);
    std::set<std::string> get_tags(const std::string& uri);
    bool has_tag(const std::string& uri, const std::string& tag);
    std::vector<std::string> get_available_tags();
    std::vector<std::string> get_uris_by_tag(const std::string& tag);
    std::unordered_map<std::string, int> get_tag_counts();
    void ensure_tag_defined(const std::string& tag, int sort_order = 999);
    int delete_unused_tags();

private:
    void init_db();

    tag_manager();
    ~tag_manager();

    tag_manager(const tag_manager&) = delete;
    tag_manager& operator=(const tag_manager&) = delete;

    std::unique_ptr<hosting::db::sqlite> db_;
    std::mutex mutex_;
};

} // namespace rouen::helpers
