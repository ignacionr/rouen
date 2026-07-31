/**
 * Google Test for RSS Watermark
 * Purpose: Test RSS feed item watermark updates, in-memory synchronization, and conflict preservation
 * Category: Bug Fix / Integration Testing
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <optional>
#include <vector>
#include <tuple>
#include <cstdlib>
#include <thread>
#include "../src/cards/information/rss.hpp"

namespace rouen::cards {
    std::shared_ptr<hosts::RSSHost> rss::getHost() {
        return nullptr;
    }
}

// Redirect HOME to a temp directory for database isolation before including RSS host
class RSSWatermarkEnvironment {
public:
    static std::filesystem::path setup_temp_home() {
        auto test_root = std::filesystem::temp_directory_path() / "test_rss_host_home";
        std::filesystem::remove_all(test_root);
        std::filesystem::create_directories(test_root);
        
        #ifdef _WIN32
        _putenv_s("APPDATA", test_root.string().c_str());
        #else
        setenv("HOME", test_root.string().c_str(), 1);
        #endif
        return test_root;
    }
};

// Include headers
#include "../src/helpers/platform_utils.hpp"
#include "../src/helpers/media_player.hpp"
#include "../src/helpers/media_player_item.hpp"
#include "../src/hosts/rss_host.hpp"

// Global/static pointer to keep the host alive to prevent background thread cleanup crashes
static std::shared_ptr<rouen::hosts::RSSHost> g_keep_alive_host = nullptr;

class RSSWatermarkTest : public ::testing::Test {
protected:
    std::filesystem::path temp_home;
    
    void SetUp() override {
        temp_home = RSSWatermarkEnvironment::setup_temp_home();
        
        // Register mock notify service globally for the test duration
        auto notify_mock = std::make_shared<std::function<void(std::string const&)>>([](std::string const& msg) {
            std::cout << "[MOCK NOTIFY] " << msg << std::endl;
        });
        registrar::add("notify", notify_mock);

        // Register mock quitting service globally for the test duration
        auto quitting_mock = std::make_shared<std::function<bool()>>([]() {
            return false;
        });
        registrar::add("quitting", quitting_mock);
    }
    
    void TearDown() override {
        g_keep_alive_host = nullptr;
        std::filesystem::remove_all(temp_home);
    }
};

TEST_F(RSSWatermarkTest, InMemoryAndDatabaseWatermarkSync) {
    // 1. Manually prepare database with a feed and item so RSSHost loads them on startup
    auto db_path = rouen::platform::get_user_data_path("rss.db");
    {
        media::rss::sqliterepo init_repo(db_path.string());
        long long feed_id = init_repo.upsert_feed("https://example.com/feed", "Example Feed", "https://example.com/image.png");
        
        std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string>> items;
        items.emplace_back(
            "Episode 1",
            "https://example.com/podcast.mp3",
            "https://example.com/item1",
            "Episode description",
            "2026-07-06 12:00:00",
            "https://example.com/image.png"
        );
        init_repo.batch_upsert_items(feed_id, items);
    } // Closes DB connection
    
    // 2. Instantiate RSSHost (loads database items into memory)
    g_keep_alive_host = std::make_shared<rouen::hosts::RSSHost>();
    auto host = g_keep_alive_host;
    
    // Verify feeds are loaded
    ASSERT_GE(host->feeds().size(), 1);
    
    // Find our test feed by URL
    std::shared_ptr<media::rss::feed> feed = nullptr;
    for (const auto& f : host->feeds()) {
        if (f->feed_link == "https://example.com/feed" || f->source_link == "https://example.com/feed") {
            feed = f;
            break;
        }
    }
    
    ASSERT_NE(feed, nullptr);
    ASSERT_EQ(feed->items.size(), 1);
    EXPECT_FALSE(feed->items[0].watermark.has_value());
    
    // 3. Trigger the watermark save callback (simulates media player notifying save)
    ASSERT_TRUE(media_player_item::save_watermark_cb != nullptr);
    media_player_item::save_watermark_cb(feed->repo_id, "https://example.com/item1", "Episode 1", 125.5);
    
    // 4. Verify in-memory representation was updated immediately
    EXPECT_TRUE(feed->items[0].watermark.has_value());
    EXPECT_DOUBLE_EQ(*feed->items[0].watermark, 125.5);
    
    // 5. Verify database was updated
    {
        media::rss::sqliterepo check_repo(db_path.string());
        bool found = false;
        check_repo.scan_items(feed->repo_id, [&](const char* link, const char* enclosure, const char* title,
                                                 const char* description, const char* pub_date, const char* image_url,
                                                 std::optional<double> watermark, std::optional<double> media_duration_seconds) {
            (void)link; (void)enclosure; (void)description; (void)pub_date; (void)image_url; (void)media_duration_seconds;
            if (std::string(title) == "Episode 1") {
                found = true;
                ASSERT_TRUE(watermark.has_value());
                EXPECT_DOUBLE_EQ(*watermark, 125.5);
            }
        });
        EXPECT_TRUE(found);
    }
    
    // 6. Give background thread time to fail/log safely before test finishes
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

TEST_F(RSSWatermarkTest, StopAllSavesPreviousItemWatermark) {
    // 1. Manually prepare database with two items
    auto db_path = rouen::platform::get_user_data_path("rss.db");
    long long feed_id = 0;
    {
        media::rss::sqliterepo init_repo(db_path.string());
        feed_id = init_repo.upsert_feed("https://example.com/feed", "Example Feed", "https://example.com/image.png");
        
        std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string>> items;
        items.emplace_back(
            "Episode 1",
            "https://example.com/podcast1.mp3",
            "https://example.com/item1",
            "Episode 1 description",
            "2026-07-06 12:00:00",
            "https://example.com/image.png"
        );
        items.emplace_back(
            "Episode 2",
            "https://example.com/podcast2.mp3",
            "https://example.com/item2",
            "Episode 2 description",
            "2026-07-06 12:01:00",
            "https://example.com/image.png"
        );
        init_repo.batch_upsert_items(feed_id, items);
    }

    g_keep_alive_host = nullptr;
    auto host = std::make_shared<rouen::hosts::RSSHost>();
    g_keep_alive_host = host;

    // Get the two media_player_item instances from the static items map
    auto& items_map = media_player::items();
    items_map.clear(); // Clear to ensure clean state

    // Retrieve two different items
    auto itemA = std::make_shared<media_player_item>();
    itemA->feed_id = feed_id;
    itemA->item_link = "https://example.com/item1";
    itemA->item_title = "Episode 1";
    itemA->position = 45.0; // Simulate we played 45 seconds of Episode 1
    items_map[111] = itemA;

    auto itemB = std::make_shared<media_player_item>();
    itemB->feed_id = feed_id;
    itemB->item_link = "https://example.com/item2";
    itemB->item_title = "Episode 2";
    items_map[222] = itemB;

    // Stop all should trigger watermark saving for itemA because it has position > 0
    media_player::stopAll();

    // Verify in-memory of Episode 1 is updated
    auto feed = host->feeds()[0];
    bool in_memory_updated = false;
    for (auto& item : feed->items) {
        if (item.title == "Episode 1") {
            if (item.watermark.has_value()) {
                EXPECT_DOUBLE_EQ(*item.watermark, 45.0);
                in_memory_updated = true;
            }
        }
    }
    EXPECT_TRUE(in_memory_updated);

    // Verify database was updated for Episode 1
    {
        media::rss::sqliterepo check_repo(db_path.string());
        bool found = false;
        check_repo.scan_items(feed_id, [&](const char* link, const char* enclosure, const char* title,
                                                 const char* description, const char* pub_date, const char* image_url,
                                                 std::optional<double> watermark, std::optional<double> media_duration_seconds) {
            (void)link; (void)enclosure; (void)description; (void)pub_date; (void)image_url; (void)media_duration_seconds;
            if (std::string(title) == "Episode 1") {
                found = true;
                ASSERT_TRUE(watermark.has_value());
                EXPECT_DOUBLE_EQ(*watermark, 45.0);
            }
        });
        EXPECT_TRUE(found);
    }
}

TEST_F(RSSWatermarkTest, ResetWatermarkToZeroOnCompletion) {
    // 1. Manually prepare database with one item
    auto db_path = rouen::platform::get_user_data_path("rss.db");
    long long feed_id = 0;
    {
        media::rss::sqliterepo init_repo(db_path.string());
        feed_id = init_repo.upsert_feed("https://example.com/feed", "Example Feed", "https://example.com/image.png");
        
        std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string>> items;
        items.emplace_back(
            "Episode 1",
            "https://example.com/podcast1.mp3",
            "https://example.com/item1",
            "Episode 1 description",
            "2026-07-06 12:00:00",
            "https://example.com/image.png"
        );
        init_repo.batch_upsert_items(feed_id, items);
    }

    g_keep_alive_host = nullptr;
    auto host = std::make_shared<rouen::hosts::RSSHost>();
    g_keep_alive_host = host;

    auto& items_map = media_player::items();
    items_map.clear();

    auto item = std::make_shared<media_player_item>();
    item->feed_id = feed_id;
    item->item_link = "https://example.com/item1";
    item->item_title = "Episode 1";
    
    // Simulate we played almost to the end of the 100-second episode
    item->duration = 100.0;
    item->position = 99.0; 
    items_map[111] = item;

    // Stop Media should detect that we are close to duration, and reset watermark to 0.0
    item->stopMedia();

    // Verify in-memory of Episode 1 is updated to 0.0
    auto feed = host->feeds()[0];
    bool in_memory_updated = false;
    for (auto& feed_item : feed->items) {
        if (feed_item.title == "Episode 1") {
            if (feed_item.watermark.has_value()) {
                EXPECT_DOUBLE_EQ(*feed_item.watermark, 0.0);
                in_memory_updated = true;
            }
        }
    }
    EXPECT_TRUE(in_memory_updated);

    // Verify database was updated to 0.0
    {
        media::rss::sqliterepo check_repo(db_path.string());
        bool found = false;
        check_repo.scan_items(feed_id, [&](const char* link, const char* enclosure, const char* title,
                                                 const char* description, const char* pub_date, const char* image_url,
                                                 std::optional<double> watermark, std::optional<double> media_duration_seconds) {
            (void)link; (void)enclosure; (void)description; (void)pub_date; (void)image_url; (void)media_duration_seconds;
            if (std::string(title) == "Episode 1") {
                found = true;
                ASSERT_TRUE(watermark.has_value());
                EXPECT_DOUBLE_EQ(*watermark, 0.0);
            }
        });
        EXPECT_TRUE(found);
    }
}

TEST_F(RSSWatermarkTest, ResetWatermarkToZeroOnNaturalExit) {
    // 1. Manually prepare database with one item
    auto db_path = rouen::platform::get_user_data_path("rss.db");
    long long feed_id = 0;
    {
        media::rss::sqliterepo init_repo(db_path.string());
        feed_id = init_repo.upsert_feed("https://example.com/feed", "Example Feed", "https://example.com/image.png");
        
        std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string>> items;
        items.emplace_back(
            "Episode 1",
            "https://example.com/podcast1.mp3",
            "https://example.com/item1",
            "Episode 1 description",
            "2026-07-06 12:00:00",
            "https://example.com/image.png"
        );
        init_repo.batch_upsert_items(feed_id, items);
    }

    g_keep_alive_host = nullptr;
    auto host = std::make_shared<rouen::hosts::RSSHost>();
    g_keep_alive_host = host;

    decltype(host->feeds())::value_type target_feed;
    for (const auto& f : host->feeds()) {
        if (f->feed_link == "https://example.com/feed" || f->source_link == "https://example.com/feed") {
            target_feed = f;
            break;
        }
    }
    ASSERT_NE(target_feed, nullptr);

    auto& items_map = media_player::items();
    items_map.clear();

    auto item = std::make_shared<media_player_item>();
    item->feed_id = target_feed->repo_id;
    item->item_link = "https://example.com/item1";
    item->item_title = "Episode 1";
    
    // Simulate process exited naturally (we set player_pid to a dummy value, but waitpid won't find it)
    item->player_pid = 999999; // Dummy PID that is not our child
    item->duration = 100.0;
    item->position = 98.5; // Almost at the end
    items_map[111] = item;

    // Call stopMedia, which should detect exit, stop playback, and reset watermark
    item->stopMedia();
    bool active = item->checkMediaStatus();
    EXPECT_FALSE(active);

    // Verify in-memory of Episode 1 is updated to 0.0
    bool in_memory_updated = false;
    for (auto& feed_item : target_feed->items) {
        if (feed_item.title == "Episode 1") {
            if (feed_item.watermark.has_value()) {
                EXPECT_DOUBLE_EQ(*feed_item.watermark, 0.0);
                in_memory_updated = true;
            }
        }
    }
    EXPECT_TRUE(in_memory_updated);
}
