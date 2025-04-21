#pragma once

#include <algorithm>
#include <atomic>
#include <ctime>
#include <functional>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>

#include "../../registrar.hpp"
#include "../../helpers/fetch.hpp"
#include "feed.hpp"
#include "sqliterepo.hpp"

namespace media::rss
{
    struct host
    {
        host(std::function<std::string(std::string_view)> system_runner) : system_runner_(system_runner)
        {
            std::vector<std::string> urls;
            repo_.scan_feeds([this, &urls](long long feed_id, const char * url, const char * title, const char * image_url) {
                auto feed_ptr = std::make_shared<media::rss::feed>(system_runner_);
                feed_ptr->feed_title = title;
                feed_ptr->source_link = url;
                feed_ptr->feed_link = url;
                feed_ptr->set_image(image_url);
                feed_ptr->repo_id = feed_id;
                repo_.scan_items(feed_id, [feed_ptr](const char * link, const char * enclosure, const char * title, const char * description, const char * pub_date, const char * image_url) {
                    // the published date time appears as a string of format "2024-12-07 06:49:08"
                    std::tm tm = {};
                    std::istringstream ss(pub_date);
                    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
                    if (ss.fail()) {
                        throw std::runtime_error("Failed to parse date");
                    }
                    auto const published_date = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                    feed_ptr->items.emplace_back(media::rss::feed::item{
                        title, link, description, enclosure, image_url, published_date});
                });
                auto feeds = feeds_.load();
                feeds->emplace_back(feed_ptr);
                urls.emplace_back(url);
            });
            add_feeds(std::move(urls));
        }

        ~host() {
            // make sure the thread ends before the repo is destroyed
            fetch_thread_.request_stop();
        }

        static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp)
        {
            auto parser = static_cast<rss::feed *>(userp);
            (*parser)(std::string_view{static_cast<char *>(contents), size * nmemb});
            return size * nmemb;
        }

        void add_feeds(std::vector<std::string> urls)
        {
            fetch_thread_ = std::jthread([this, urls] {
                auto quit_job = "quitting"_fnb;
                for (auto const &url_str : urls) {
                    try {
                        add_feed_sync(url_str, quit_job);
                    } 
                    catch(std::exception const &e) {
                        "notify"_sfn(std::format("Failed to add feed {}: {}\n", url_str, e.what()));
                    }
                    catch(...) {
                        "notify"_sfn(std::format("Failed to add feed {}\n", url_str));
                    }
                    if (quit_job()) break;
                }
                });
        }

        std::shared_ptr<media::rss::feed> add_feed_sync(std::string_view url, auto quitting)
        {
            auto feed_ptr = std::make_shared<media::rss::feed>(get_feed(url, system_runner_));
            {
                if (quitting()) return nullptr;

                static std::mutex mutex;
                std::lock_guard<std::mutex> lock(mutex);
                auto feeds = feeds_.load();
                // does the feed already exist?
                auto pos = std::find_if(feeds->begin(), feeds->end(),
                                        [ourlink = feed_ptr->feed_link, oursrc = feed_ptr->source_link](auto const &f)
                                        {
                                            return f->feed_link == ourlink || f->source_link == oursrc;
                                        });
                // add or merge with the new one
                if (pos != feeds->end())
                {
                    // update the feed
                    feed_ptr->repo_id = (*pos)->repo_id;
                    (*pos)->feed_title = feed_ptr->feed_title;
                    (*pos)->feed_description = feed_ptr->feed_description;
                    (*pos)->feed_link = feed_ptr->feed_link;
                    (*pos)->set_image(feed_ptr->image_url());
                    // now merge all items, avoiding duplicates
                    for (auto const &item : feed_ptr->items)
                    {
                        auto item_pos = std::find_if((*pos)->items.begin(), (*pos)->items.end(),
                                                     [ourlink = item.link](auto const &i)
                                                     {
                                                         return i.link == ourlink;
                                                     });
                        if (item_pos == (*pos)->items.end())
                        {
                            (*pos)->items.emplace_back(item);
                        }
                    }
                    feed_ptr = *pos;
                }
                else
                {
                    feeds->emplace_back(feed_ptr);
                }
                feed_ptr->repo_id = repo_.upsert_feed(url, feed_ptr->feed_title, feed_ptr->image_url());
                // update the items
                for (auto const &item : feed_ptr->items)
                {
                    // format the date time
                    auto const pub_date = std::format("{:%F %T}", item.updated, item.updated);
                    repo_.upsert_item(feed_ptr->repo_id, 
                        item.title, 
                        item.enclosure,
                        item.link, 
                        item.description, 
                        pub_date, 
                        item.image_url);
                }
                // sort the feeds from the latest updated to the oldest
                std::sort(feeds->begin(), feeds->end(),
                          [](auto const &lhs, auto const &rhs)
                          {
                              return lhs->items.empty() ? false : (rhs->items.empty() ? true : lhs->items.front().updated > rhs->items.front().updated);
                          });
                feeds_.store(feeds);
            }
            return feed_ptr;
        }

        auto feeds()
        {
            return *feeds_.load();
        }

        void delete_feed(std::string_view url)
        {
            auto feeds = feeds_.load();
            auto pos = std::find_if(feeds->begin(), feeds->end(),
                                    [url](auto const &f)
                                    {
                                        return f->feed_link == url;
                                    });
            if (pos != feeds->end())
            {
                feeds->erase(pos);
                feeds_.store(feeds);
                repo_.delete_feed(url);
            }
        }

    private:
        std::atomic<std::shared_ptr<std::vector<std::shared_ptr<rss::feed>>>> feeds_ = std::make_shared<std::vector<std::shared_ptr<rss::feed>>>();
        std::function<std::string(std::string_view)> system_runner_;
        std::jthread fetch_thread_;

        static feed get_feed(std::string_view url, std::function<std::string(std::string_view)> system_runner)
        {
            http::fetch fetch;
            feed parser{system_runner};
            parser.source_link = url;
            fetch(std::string{url}, [](auto h){}, writeCallback, &parser);
            return parser;
        }
        sqliterepo repo_{"rss.db"};
    };
}