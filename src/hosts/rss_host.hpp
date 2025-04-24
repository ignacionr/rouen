#pragma once

#include <algorithm>
#include <atomic>
#include <ctime>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
#include <iostream>

#include "../registrar.hpp"
#include "../helpers/fetch.hpp"
#include "../helpers/debug.hpp"
#include "../models/rss/feed.hpp"
#include "../models/rss/sqliterepo.hpp"

namespace rouen::hosts {

/**
 * RSS Host Controller
 * 
 * This class acts as a controller for RSS feeds, managing the communication
 * between the UI (cards) and the data model (feeds and SQLite repository).
 * It provides methods for adding, removing, and accessing RSS feeds.
 */
class RSSHost {
public:
    // Feed item representation that can be used by UI components
    struct FeedItem {
        std::string title;
        std::string description;
        std::string link;
        std::string enclosure;
        std::string image_url;
        std::chrono::system_clock::time_point publish_date;
    };

    // Feed information structure
    struct FeedInfo {
        long long id;
        std::string title;
        std::string url;
        std::string image_url;
    };

    /**
     * Constructor initializes the RSS host with a system runner and loads existing feeds
     */
    RSSHost(std::function<std::string(std::string_view)> system_runner) 
        : system_runner_(system_runner),
          repo_("rss.db")
    {
        RSS_INFO("RSSHost constructor starting...");
        // Load existing feeds but defer loading items until they're needed
        std::vector<std::string> urls;
        
        try {
            RSS_INFO("RSSHost scanning feeds from repository...");
            repo_.scan_feeds([this, &urls](long long feed_id, const char* url, const char* title, const char* image_url) {
                RSS_DEBUG_FMT("Found feed: ID={}, URL={}", feed_id, (url ? url : "null"));
                
                auto feed_ptr = std::make_shared<media::rss::feed>(system_runner_);
                feed_ptr->feed_title = title ? title : "";
                feed_ptr->source_link = url ? url : "";
                feed_ptr->feed_link = url ? url : "";
                feed_ptr->set_image(image_url ? image_url : "");
                feed_ptr->repo_id = feed_id;
                
                // Don't load items here - they'll be loaded on demand
                // Just record the feed metadata
                auto feeds = feeds_.load();
                feeds->emplace_back(feed_ptr);
                urls.emplace_back(url ? url : "");
                RSS_DEBUG_FMT("Added feed ID={} to collection (deferred loading)", feed_id);
            });
        } catch (const std::exception& e) {
            RSS_ERROR_FMT("Exception during RSSHost feed scanning: {}", e.what());
        }
        
        // Refresh feeds (this will happen in a background thread)
        RSS_INFO("RSSHost starting feed refresh in background thread...");
        refreshFeeds(std::move(urls));
        RSS_INFO("RSSHost constructor completed");
    }

    ~RSSHost() {
        RSS_INFO("RSSHost destructor starting...");
        fetch_thread_.request_stop();
        RSS_INFO("RSSHost destructor completed");
    }

    /**
     * Add new feeds from a list of URLs
     */
    void addFeeds(std::vector<std::string> urls) {
        refreshFeeds(std::move(urls));
    }

    /**
     * Add a new feed by URL
     */
    bool addFeed(const std::string& url) {
        try {
            std::vector<std::string> urls = {url};
            refreshFeeds(std::move(urls));
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    /**
     * Get all feeds
     */
    auto feeds() const {
        return *feeds_.load();
    }

    /**
     * Delete a feed by URL
     */
    void deleteFeed(std::string_view url) {
        auto feeds = feeds_.load();
        auto pos = std::find_if(feeds->begin(), feeds->end(),
                               [url](auto const& f) {
                                   return f->feed_link == url || f->source_link == url;
                               });
        
        if (pos != feeds->end()) {
            feeds->erase(pos);
            feeds_.store(feeds);
            repo_.delete_feed(url);
        }
    }

    /**
     * Get feed information by ID
     */
    std::optional<FeedInfo> getFeedInfo(long long feed_id) {
        std::optional<FeedInfo> result;
        
        repo_.scan_feeds([&result, feed_id](long long id, const char* url, const char* title, const char* image_url) {
            if (id == feed_id) {
                result = FeedInfo{
                    .id = id,
                    .title = title ? title : "",
                    .url = url ? url : "",
                    .image_url = image_url ? image_url : ""
                };
            }
        });
        
        return result;
    }

    /**
     * Get items for a specific feed
     */
    std::vector<FeedItem> getFeedItems(long long feed_id) {
        std::vector<FeedItem> items;
        
        repo_.scan_items(feed_id, [&items](const char* link, const char* enclosure, const char* title, 
                                         const char* description, const char* pub_date, const char* image_url) {
            // Parse the date string
            std::tm tm = {};
            std::istringstream ss(pub_date);
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
            auto publish_date = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            
            // Create and store the item
            FeedItem item{
                .title = title ? title : "",
                .description = description ? description : "",
                .link = link ? link : "",
                .enclosure = enclosure ? enclosure : "",
                .image_url = image_url ? image_url : "",
                .publish_date = publish_date
            };
            
            items.push_back(std::move(item));
        });
        
        // Sort items by publish date (newest first)
        std::sort(items.begin(), items.end(), [](const FeedItem& a, const FeedItem& b) {
            return a.publish_date > b.publish_date;
        });
        
        return items;
    }
    
    /**
     * Get a specific item from a feed by its link
     */
    std::optional<FeedItem> getFeedItem(long long feed_id, const std::string& item_link) {
        std::optional<FeedItem> result;
        
        repo_.scan_items(feed_id, [&result, &item_link](const char* link, const char* enclosure, const char* title, 
                                                     const char* description, const char* pub_date, const char* image_url) {
            if (link && item_link == link) {
                // Parse the date string
                std::tm tm = {};
                std::istringstream ss(pub_date);
                ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
                auto publish_date = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                
                result = FeedItem{
                    .title = title ? title : "",
                    .description = description ? description : "",
                    .link = link,
                    .enclosure = enclosure ? enclosure : "",
                    .image_url = image_url ? image_url : "",
                    .publish_date = publish_date
                };
            }
        });
        
        return result;
    }

private:
    // Callback for the HTTP fetch operation
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        auto parser = static_cast<media::rss::feed*>(userp);
        (*parser)(std::string_view{static_cast<char*>(contents), size * nmemb});
        return size * nmemb;
    }

    // Refresh feeds in a background thread
    void refreshFeeds(std::vector<std::string> urls) {
        fetch_thread_ = std::jthread([this, urls] (std::stop_token stoken) {
            auto quit_job = [stoken]() -> bool {
                return "quitting"_fnb() || stoken.stop_requested();
            };
            for (const auto& url_str : urls) {
                try {
                    addFeedSync(url_str, quit_job);
                } 
                catch (const std::exception& e) {
                    "notify"_sfn(std::format("Failed to add feed {}: {}\n", url_str, e.what()));
                }
                catch (...) {
                    "notify"_sfn(std::format("Failed to add feed {}\n", url_str));
                }
                if (quit_job()) break;
            }
        });
    }

    // Synchronously add a feed
    std::shared_ptr<media::rss::feed> addFeedSync(std::string_view url, auto quitting) {
        try {
            // Download and parse the feed
            auto feed_ptr = std::make_shared<media::rss::feed>(getFeed(url, system_runner_));
            
            if (quitting()) return nullptr;

            static std::mutex mutex;
            std::lock_guard<std::mutex> lock(mutex);
            auto feeds = feeds_.load();
            
            // Check if the feed already exists
            auto pos = std::find_if(feeds->begin(), feeds->end(),
                                  [ourlink = feed_ptr->feed_link, oursrc = feed_ptr->source_link](auto const& f) {
                                      return f->feed_link == ourlink || f->source_link == oursrc;
                                  });
                                  
            // Add or merge with existing feed
            if (pos != feeds->end()) {
                // Update the existing feed
                feed_ptr->repo_id = (*pos)->repo_id;
                (*pos)->feed_title = feed_ptr->feed_title;
                (*pos)->feed_description = feed_ptr->feed_description;
                (*pos)->feed_link = feed_ptr->feed_link;
                (*pos)->set_image(feed_ptr->image_url());
                
                // Merge new items, avoiding duplicates
                for (auto const& item : feed_ptr->items) {
                    auto item_pos = std::find_if((*pos)->items.begin(), (*pos)->items.end(),
                                              [ourlink = item.link](auto const& i) {
                                                  return i.link == ourlink;
                                              });
                    if (item_pos == (*pos)->items.end()) {
                        (*pos)->items.emplace_back(item);
                    }
                }
                feed_ptr = *pos;
            } else {
                feeds->emplace_back(feed_ptr);
            }
            
            // Update the repository with feed info
            feed_ptr->repo_id = repo_.upsert_feed(url, feed_ptr->feed_title, feed_ptr->image_url());
            
            // Prepare items for batch insert
            std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string>> items_batch;
            items_batch.reserve(feed_ptr->items.size());
            
            for (auto const& item : feed_ptr->items) {
                // Format the date time
                auto const pub_date = std::format("{:%F %T}", item.updated, item.updated);
                
                // Add to batch collection
                items_batch.emplace_back(
                    item.title,
                    item.enclosure,
                    item.link,
                    item.description,
                    pub_date,
                    item.image_url
                );
            }
            
            // Perform batch insert for better performance
            if (!items_batch.empty()) {
                repo_.batch_upsert_items(feed_ptr->repo_id, items_batch);
            }
            
            // Sort the feeds from the latest updated to the oldest
            std::sort(feeds->begin(), feeds->end(),
                    [](auto const& lhs, auto const& rhs) {
                        return lhs->items.empty() ? false : 
                              (rhs->items.empty() ? true : 
                                lhs->items.front().updated > rhs->items.front().updated);
                    });
                    
            feeds_.store(feeds);
            return feed_ptr;
        } catch (const std::exception& e) {
            // Log the error and rethrow for handling in the caller
            "notify"_sfn(std::format("Error processing feed {}: {}", url, e.what()));
            throw;
        }
    }

    // Fetch and parse a feed from a URL with improved error handling
    static media::rss::feed getFeed(std::string_view url, std::function<std::string(std::string_view)> system_runner) {
        try {
            http::fetch fetch(60); // Increase timeout for large feeds
            media::rss::feed parser{system_runner};
            parser.source_link = url;
            
            // Set up proper headers for better compatibility
            auto header_client = [](auto h) {
                h("User-Agent: Rouen RSS Reader/1.0");
                h("Accept: application/rss+xml, application/xml, text/xml, */*");
            };
            
            fetch(std::string{url}, header_client, writeCallback, &parser);
            return parser;
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to fetch feed: ") + e.what());
        }
    }

private:
    // Thread-safe collection of feeds
    std::atomic<std::shared_ptr<std::vector<std::shared_ptr<media::rss::feed>>>> feeds_ = 
        std::make_shared<std::vector<std::shared_ptr<media::rss::feed>>>();
    
    std::function<std::string(std::string_view)> system_runner_;
    std::jthread fetch_thread_;
    media::rss::sqliterepo repo_;
};

} // namespace rouen::hosts