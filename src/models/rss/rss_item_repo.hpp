#pragma once
#include <string>
#include <vector>
#include <tuple>
#include <mutex>

namespace media::rss {
    class rss_item_repo {
    public:
        rss_item_repo(void* db_ptr, std::mutex* mtx);
        void batch_upsert_items(long long feed_id, const std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string>>& items);
        void upsert_item(long long feed_id, std::string_view title, std::string_view enclosure, std::string_view link, std::string_view description, std::string_view pub_date, std::string_view image_url);
        
        template<typename Sink>
        void scan_items(long long feed_id, Sink sink);
    private:
        void* db_ptr_;
        std::mutex* mutex_;
    };
}
