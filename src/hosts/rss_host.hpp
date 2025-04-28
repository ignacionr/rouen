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
#include <fstream>
#include <filesystem>

// Include our compatibility layer for C++20/23 features
#include "../helpers/compat/compat.hpp"

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
    RSSHost() 
        : repo_("rss.db")
    {
        RSS_INFO("RSSHost constructor starting...");
        // Load existing feeds but defer loading items until they're needed
        std::vector<std::string> urls;
        
        try {
            RSS_INFO("RSSHost scanning feeds from repository...");
            repo_.scan_feeds([this, &urls](long long feed_id, const char* url, const char* title, const char* image_url) {
                RSS_DEBUG_FMT("Found feed: ID={}, URL={}", feed_id, (url ? url : "null"));
                
                auto feed_ptr = std::make_shared<media::rss::feed>();
                feed_ptr->feed_title = title ? title : "";
                feed_ptr->source_link = url ? url : "";
                feed_ptr->feed_link = url ? url : "";
                feed_ptr->set_image(image_url ? image_url : "");
                feed_ptr->repo_id = feed_id;
                
                // Don't load items here - they'll be loaded on demand
                // Just record the feed metadata
                std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
                feeds_.emplace_back(feed_ptr);
                urls.emplace_back(url ? url : "");
                RSS_DEBUG_FMT("Added feed ID={} to collection (deferred loading)", feed_id);
            });
        } catch (const std::exception& e) {
            RSS_ERROR_FMT("Exception during RSSHost feed scanning: {}", e.what());
        }
        
        // Load podcasts from podcasts.txt file if it exists
        // This must happen AFTER loading existing feeds so we can properly check for duplicates
        loadPodcastsFromFile();
        
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
        RSS_WARN_FMT("addFeeds called with {} URLs", urls.size());
        for (const auto& url : urls) {
            RSS_WARN_FMT("URL being added: {}", url);
        }
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
        // Need to use const_cast because we need to lock the mutex in a const method
        std::lock_guard<std::mutex> feeds_lock(*const_cast<std::mutex*>(&feeds_mutex_));
        return feeds_;
    }

    /**
     * Delete a feed by URL
     */
    void deleteFeed(std::string_view url) {
        std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
        auto pos = std::find_if(feeds_.begin(), feeds_.end(),
                               [url](auto const& f) {
                                   return f->feed_link == url || f->source_link == url;
                               });
        
        if (pos != feeds_.end()) {
            feeds_.erase(pos);
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

    /**
     * Load podcasts from a file if it exists and directly add them to the database
     * This checks for a podcasts.txt file in the current directory
     * and adds any RSS feed URLs found in the file
     */
    void loadPodcastsFromFile() {
        RSS_WARN("Checking for podcasts.txt file...");
        const std::string filename = "podcasts.txt";
        
        // Check if file exists before attempting to read
        if (!std::filesystem::exists(filename)) {
            RSS_WARN("podcasts.txt file not found, skipping");
            return;
        }
        
        // Get absolute path for better debugging
        std::filesystem::path abs_path = std::filesystem::absolute(filename);
        RSS_WARN_FMT("Reading podcasts from {} file (absolute path: {})", filename, abs_path.string());
        
        std::ifstream file(filename);
        if (!file.is_open()) {
            RSS_ERROR_FMT("Failed to open {} file", filename);
            return;
        }
        
        // Read the file line by line
        std::string line;
        std::vector<std::string> podcast_urls;
        int line_count = 0;
        int added_count = 0;
        
        while (std::getline(file, line)) {
            line_count++;
            
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') {
                RSS_DEBUG_FMT("Line {}: Skipping comment or empty line", line_count);
                continue;
            }
            
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t"));
            if (!line.empty() && line.find_last_not_of(" \t") != std::string::npos) {
                line.erase(line.find_last_not_of(" \t") + 1);
            }
            
            if (!line.empty()) {
                podcast_urls.push_back(line);
                RSS_WARN_FMT("Line {}: Found podcast URL: {}", line_count, line);
            }
        }
        
        RSS_WARN_FMT("Found {} URLs in podcasts.txt", podcast_urls.size());
        
        if (podcast_urls.empty()) {
            RSS_WARN_FMT("No valid podcast URLs found in {}", filename);
            return;
        }
        
        RSS_WARN_FMT("Processing {} podcasts from {}", podcast_urls.size(), filename);
        
        // Directly process each URL and add it to the database
        for (const auto& url : podcast_urls) {
            // First check if this URL already exists in the repository
            bool exists = false;
            {
                std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
                for (const auto& feed : feeds_) {
                    if (feed->source_link == url || feed->feed_link == url) {
                        RSS_WARN_FMT("Podcast already exists: {}", url);
                        exists = true;
                        break;
                    }
                }
            }
            
            if (!exists) {
                RSS_WARN_FMT("Adding new podcast URL to database: {}", url);
                // Directly add to database with default values
                long long feed_id = repo_.upsert_feed(url, url, "");
                
                if (feed_id > 0) {
                    // Create minimal feed representation for the UI
                    auto feed_ptr = std::make_shared<media::rss::feed>();
                    feed_ptr->feed_title = url;  // Use URL as title initially
                    feed_ptr->source_link = url;
                    feed_ptr->feed_link = url;
                    feed_ptr->repo_id = feed_id;
                    
                    // Add to in-memory collection
                    std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
                    feeds_.emplace_back(feed_ptr);
                    added_count++;
                    RSS_WARN_FMT("Added feed ID={} to database and memory", feed_id);
                } else {
                    RSS_ERROR_FMT("Failed to add podcast to database: {}", url);
                }
            }
        }
        
        RSS_WARN_FMT("Directly added {} new podcasts from {}", added_count, filename);
        
        if (added_count > 0) {
            "notify"_sfn(std::format("Added {} new podcasts from {}", added_count, filename));
        }
    }

    /**
     * Refresh a specific feed by ID
     * Returns true if the refresh was successful
     */
    bool refreshFeed(long long feed_id) {
        try {
            RSS_INFO_FMT("Refreshing feed with ID: {}", feed_id);
            
            // Find the feed URL from the repository
            std::optional<std::string> feed_url;
            
            repo_.scan_feeds([&feed_url, feed_id](long long id, const char* url, const char* /*title*/, const char* /*image_url*/) {
                if (id == feed_id && url) {
                    feed_url = url;
                }
            });
            
            if (!feed_url) {
                RSS_ERROR_FMT("Could not find URL for feed ID: {}", feed_id);
                return false;
            }
            
            // Use a lambda to bypass the quit check in sync feed refresh
            auto never_quit = []() { return false; };
            
            // Fetch the feed synchronously
            auto refreshed_feed = addFeedSync(*feed_url, never_quit);
            
            if (refreshed_feed) {
                RSS_INFO_FMT("Successfully refreshed feed ID: {}", feed_id);
                return true;
            } else {
                RSS_ERROR_FMT("Failed to refresh feed ID: {}", feed_id);
                return false;
            }
        } 
        catch (const std::exception& e) {
            RSS_ERROR_FMT("Exception while refreshing feed ID {}: {}", feed_id, e.what());
            return false;
        }
    }

private:
    // Callback for the HTTP fetch operation
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        auto parser = static_cast<media::rss::feed*>(userp);
        (*parser)(std::string_view{static_cast<char*>(contents), size * nmemb});
        return size * nmemb;
    }

    // Refresh feeds in a background thread with performance improvements
    void refreshFeeds(std::vector<std::string> urls) {
        // Define how many feeds to process in parallel
        const int BATCH_SIZE = 5;
        
        fetch_thread_ = std::jthread([this, urls = std::move(urls)] (std::stop_token stoken) {
            auto quit_job = [stoken]() -> bool {
                return "quitting"_fnb() || stoken.stop_requested();
            };
            
            // Track successful feeds for notification purposes
            int success_count = 0;
            int error_count = 0;
            
            // Process feeds in batches to balance performance
            for (size_t i = 0; i < urls.size(); i += BATCH_SIZE) {
                // Create a batch of worker threads
                std::vector<std::jthread> workers;
                std::mutex results_mutex;
                std::vector<std::shared_ptr<media::rss::feed>> batch_results;
                
                // Process up to BATCH_SIZE feeds in parallel
                size_t end = std::min(i + BATCH_SIZE, urls.size());
                
                for (size_t j = i; j < end; ++j) {
                    if (quit_job()) break;
                    
                    workers.emplace_back([this, &urls, j, &results_mutex, &batch_results, &success_count, 
                                         &error_count, &quit_job](std::stop_token worker_stoken) {
                        // Create a composite quit check that includes the worker thread's stop token
                        auto worker_quit = [worker_stoken, &quit_job]() -> bool {
                            return quit_job() || worker_stoken.stop_requested();
                        };
                        
                        try {
                            // Only attempt to add the feed if we're not quitting
                            if (!worker_quit()) {
                                RSS_WARN_FMT("Starting to process feed: {}", urls[j]);
                                auto feed_ptr = addFeedSync(urls[j], worker_quit);
                                
                                if (feed_ptr) {
                                    RSS_WARN_FMT("Successfully fetched and processed feed: {}", urls[j]);
                                    std::lock_guard<std::mutex> lock(results_mutex);
                                    batch_results.push_back(feed_ptr);
                                    ++success_count;
                                    
                                    // Update the UI periodically to show progress
                                    if (success_count % 5 == 0) {
                                        "notify"_sfn(std::format("Progress: {} RSS feeds loaded so far...", success_count));
                                    }
                                }
                            }
                        } catch (const std::exception& e) {
                            RSS_ERROR_FMT("Failed to add feed {}: {}", urls[j], e.what());
                            "notify"_sfn(std::format("Failed to add feed {}", urls[j]));
                            
                            std::lock_guard<std::mutex> lock(results_mutex);
                            ++error_count;
                        } catch (...) {
                            RSS_ERROR_FMT("Failed to add feed {} with unknown error", urls[j]);
                            
                            std::lock_guard<std::mutex> lock(results_mutex);
                            ++error_count;
                        }
                    });
                }
                
                // Wait for all workers in this batch to complete
                for (auto& worker : workers) {
                    worker.join();
                }
                
                if (quit_job()) break;
                
                // If we've processed at least 10 feeds, notify the user of the current progress
                if (i + BATCH_SIZE >= 10 && success_count > 0) {
                    "notify"_sfn(std::format("Loaded {} out of {} RSS feeds so far...", 
                                             success_count, urls.size()));
                }
            }
            
            // Final notification
            if (success_count > 0) {
                "notify"_sfn(std::format("Successfully loaded {} RSS feeds. Restart the RSS card to see all feeds.", success_count));
            }
            
            if (error_count > 0) {
                "notify"_sfn(std::format("{} feeds failed to load. Check the logs for details.", error_count));
            }
        });
    }

    // Synchronously add a feed
    std::shared_ptr<media::rss::feed> addFeedSync(std::string_view url, auto quitting) {
        try {
            // Download and parse the feed
            auto feed_ptr = std::make_shared<media::rss::feed>(getFeed(url));
            
            if (quitting()) return nullptr;

            std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
            auto& feeds = feeds_;
            
            // Check if the feed already exists
            auto pos = std::find_if(feeds.begin(), feeds.end(),
                                  [ourlink = feed_ptr->feed_link, oursrc = feed_ptr->source_link](auto const& f) {
                                      return f->feed_link == ourlink || f->source_link == oursrc;
                                  });
                                  
            // Add or merge with existing feed
            if (pos != feeds.end()) {
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
                feeds.emplace_back(feed_ptr);
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
            std::sort(feeds.begin(), feeds.end(),
                    [](auto const& lhs, auto const& rhs) {
                        return lhs->items.empty() ? false : 
                              (rhs->items.empty() ? true : 
                                lhs->items.front().updated > rhs->items.front().updated);
                    });
                    
            return feed_ptr;
        } catch (const std::exception& e) {
            // Log the error and rethrow for handling in the caller
            "notify"_sfn(std::format("Error processing feed {}: {}", url, e.what()));
            throw;
        }
    }

    // Fetch and parse a feed from a URL with improved error handling
    static media::rss::feed getFeed(std::string_view url) {
        try {
            RSS_WARN_FMT("Starting feed fetch for URL: {}", url);
            http::fetch fetch{60}; // Increase timeout for large feeds
            media::rss::feed parser;
            parser.source_link = url;
            
            // Set up proper headers for better compatibility
            auto header_client = [](auto h) {
                h("User-Agent: Rouen RSS Reader/1.0");
                h("Accept: application/rss+xml, application/xml, text/xml, */*");
            };
            
            fetch(std::string{url}, header_client, writeCallback, &parser);
            RSS_WARN_FMT("Successfully fetched feed: {} - Title: {}", url, parser.feed_title);
            return parser;
        } catch (const std::exception& e) {
            RSS_ERROR_FMT("Failed to fetch feed {}: {}", url, e.what());
            throw std::runtime_error(std::string("Failed to fetch feed: ") + e.what());
        }
    }

private:
    // Thread-safe collection of feeds using mutex instead of atomic
    std::vector<std::shared_ptr<media::rss::feed>> feeds_;
    std::mutex feeds_mutex_;
    
    std::jthread fetch_thread_;
    media::rss::sqliterepo repo_;
};

} // namespace rouen::hosts