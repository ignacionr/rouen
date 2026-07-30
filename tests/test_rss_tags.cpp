/**
 * Google Test for RSS Tag Management
 * Purpose: Test add_feed_tag, remove_feed_tag, and delete_unused_tags functionality
 * Category: Unit/Integration Testing
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <string>

#include "../src/models/rss/sqliterepo.hpp"

class RSSTagTest : public ::testing::Test {
protected:
    std::string test_db_path;

    void SetUp() override {
        test_db_path = (std::filesystem::temp_directory_path() / "test_rss_tags.db").string();
        std::filesystem::remove(test_db_path);
    }

    void TearDown() override {
        std::filesystem::remove(test_db_path);
    }
};

TEST_F(RSSTagTest, DeleteUnusedTags) {
    media::rss::sqliterepo repo(test_db_path);

    // Create a feed
    long long feed_id = repo.upsert_feed("https://example.com/rss", "Example Feed", "https://example.com/icon.png");
    ASSERT_GT(feed_id, 0);

    // Default repo populates 15 initial tags in rss_tag_definition
    // Adding Tech (new) and News (matches default) gives 16 total tag definitions before cleanup
    repo.add_feed_tag(feed_id, "Tech");
    repo.add_feed_tag(feed_id, "News");

    auto tags_feed = repo.get_feed_tags(feed_id);
    EXPECT_EQ(tags_feed.size(), 2);

    auto avail_before = repo.get_available_tags();
    EXPECT_EQ(avail_before.size(), 16);

    // Remove "Tech" tag from feed, leaving it in rss_tag_definition as unused
    repo.remove_feed_tag(feed_id, "Tech");

    tags_feed = repo.get_feed_tags(feed_id);
    EXPECT_EQ(tags_feed.size(), 1);
    EXPECT_TRUE(tags_feed.contains("News"));

    // Delete unused tags (15 unused tags removed)
    int deleted = repo.delete_unused_tags();
    EXPECT_EQ(deleted, 15);


    // Available tags should now only contain "News" which is assigned to feed_id
    auto avail_after = repo.get_available_tags();
    ASSERT_EQ(avail_after.size(), 1);
    EXPECT_EQ(avail_after[0], "News");

}
