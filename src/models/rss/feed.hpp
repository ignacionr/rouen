#pragma once

#include <chrono>
#include <format>
#include <functional>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include <iomanip>

#include <tinyxml2.h>
#include "../../helpers/fetch.hpp"
#include "../../registrar.hpp"

namespace media::rss {
    struct feed {

        class item {
        public:
            std::string title;
            std::string link;
            std::string description;
            std::string enclosure;
            std::string image_url;
            std::chrono::system_clock::time_point updated;

            item() = default;
            item(std::string_view title, std::string_view link, std::string_view description,
                 std::string_view enclosure, std::string_view image_url,
                 std::chrono::system_clock::time_point updated)
                : title(title), link(link), description(description),
                  enclosure(enclosure), image_url(image_url), updated(updated) {}

            void refresh_summary() noexcept {
                try {
                    auto const link_contents = http::fetch{}(link);
                    auto const summarize = registrar::get<std::function<std::string(std::string_view)>>({});
                    auto text = (*summarize)(link_contents);
                    summary_ = text;
                }
                catch (std::exception const &e) {
                    summary_ = e.what();
                }
                catch (...) {
                    summary_ = "Failed to summarize";
                }
            }
            [[nodiscard]] std::string_view summary() noexcept {
                if (summary_.empty()) {
                    refresh_summary();
                }
                return summary_;
            }
        private:
            std::string summary_;
        };

        feed(std::function<std::string(std::string_view)> system_runner) : system_runner_(system_runner) {}

        void operator()(std::string_view partial_contents) {
            contents += partial_contents;
            tinyxml2::XMLDocument doc;
            doc.Parse(contents.c_str());
            if (doc.Error()) {
                // the document might be incomplete, so we ignore the error
            }
            else {
                (*this)(doc);
            }
        }

        static std::mutex &image_mutex() {
            static std::mutex mx;
            return mx;
        }

        void set_image(std::string const &url) {
            std::lock_guard<std::mutex> lock(image_mutex());
            feed_image_url = url;
            if (feed_image_url.ends_with("/")) {
                feed_image_url.pop_back();
            }
        }

        std::string const &image_url() const {
            std::lock_guard<std::mutex> lock(image_mutex());
            return feed_image_url;
        }

        // Helper function to parse dates in multiple formats
        std::chrono::system_clock::time_point parse_date(const char* date_str) {
            if (!date_str || !*date_str) {
                return std::chrono::system_clock::now();
            }

            std::tm tm = {};
            bool parsed = false;
            std::string date_string = date_str;

            // Try RFC 822/1123 format (RSS 2.0 standard)
            // Example: "Tue, 14 Apr 2020 18:16:11 +0000"
            {
                std::istringstream ss(date_string);
                ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S");
                if (!ss.fail()) {
                    parsed = true;
                    // Note: timezone is ignored in this simple version
                }
            }
            
            // Try ISO 8601 format (Atom standard)
            // Example: "2024-12-07T06:49:08Z"
            if (!parsed) {
                std::istringstream ss(date_string);
                tm = {};
                ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
                if (!ss.fail()) {
                    parsed = true;
                }
            }
            
            // Try SQL format (used in SQLite)
            // Example: "2024-12-07 06:49:08"
            if (!parsed) {
                std::istringstream ss(date_string);
                tm = {};
                ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
                if (!ss.fail()) {
                    parsed = true;
                }
            }
            
            // Try other common formats
            if (!parsed) {
                const std::vector<std::string> formats = {
                    "%d %b %Y %H:%M:%S",     // "14 Apr 2020 18:16:11"
                    "%b %d, %Y %H:%M:%S",    // "Apr 14, 2020 18:16:11"
                    "%Y/%m/%d %H:%M:%S",     // "2020/04/14 18:16:11"
                    "%d/%m/%Y %H:%M:%S",     // "14/04/2020 18:16:11"
                    "%m/%d/%Y %H:%M:%S",     // "04/14/2020 18:16:11"
                    "%Y-%m-%d",              // "2020-04-14" (date only)
                    "%d %b %Y",              // "14 Apr 2020" (date only)
                    "%b %d, %Y"              // "Apr 14, 2020" (date only)
                };
                
                for (const auto& format : formats) {
                    std::istringstream ss(date_string);
                    tm = {};
                    ss >> std::get_time(&tm, format.c_str());
                    if (!ss.fail()) {
                        parsed = true;
                        break;
                    }
                }
            }
            
            // Special case for malformed dates
            if (!parsed && date_string.length() > 10) {
                // Try to extract just the date part (first 10 chars) for YYYY-MM-DD
                if (date_string[4] == '-' && date_string[7] == '-') {
                    std::string just_date = date_string.substr(0, 10);
                    std::istringstream ss(just_date);
                    tm = {};
                    ss >> std::get_time(&tm, "%Y-%m-%d");
                    if (!ss.fail()) {
                        parsed = true;
                    }
                }
            }
            
            // Fallback to current time if parsing failed
            if (!parsed) {
                "notify"_sfn(std::format("Failed to parse RSS date: {}", date_string));
                return std::chrono::system_clock::now();
            }
            
            return std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }

        void operator()(tinyxml2::XMLDocument const &doc) {
            if (auto root = doc.FirstChildElement("rss"); root) {
                auto channel = root->FirstChildElement("channel");
                if (channel) {
                    auto title = channel->FirstChildElement("title");
                    if (title) {
                        feed_title = title->GetText();
                    }
                    auto link = channel->FirstChildElement("link");
                    if (link) {
                        feed_link = link->GetText();
                    }
                    auto description = channel->FirstChildElement("description");
                    if (description) {
                        feed_description = description->GetText();
                    }
                    auto image = channel->FirstChildElement("image");
                    if (image) {
                        auto url = image->FirstChildElement("url");
                        if (url) {
                            set_image(url->GetText());
                        }
                    }
                    else {
                        auto itunes_image = channel->FirstChildElement("itunes:image");
                        if (itunes_image) {
                            set_image(itunes_image->Attribute("href"));
                        }
                    }
                    for (
                        auto xml_item = channel->FirstChildElement("item");
                        xml_item;
                        xml_item = xml_item->NextSiblingElement("item")) 
                    {
                        item new_item;
                        title = xml_item->FirstChildElement("title");
                        if (title) {
                            new_item.title = title->GetText();
                        }
                        link = xml_item->FirstChildElement("link");
                        if (link) {
                            new_item.link = link->GetText();
                        }
                        description = xml_item->FirstChildElement("description");
                        if (description && !description->NoChildren()) {
                            new_item.description = description->GetText();
                        }
                        auto enclosure = xml_item->FirstChildElement("enclosure");
                        if (enclosure) {
                            new_item.enclosure = enclosure->Attribute("url");
                        }
                        // if there is no direct enclosure, we can try to get one if the link is to youtube
                        else if (new_item.link.find("youtube.com") != std::string::npos) {
                            new_item.enclosure = new_item.link;
                        }
                        // look for an itunes:image tag
                        auto itunes_image = xml_item->FirstChildElement("itunes:image");
                        if (itunes_image) {
                            new_item.image_url = itunes_image->Attribute("href");
                        }
                        // Try various date formats in priority order
                        const char* date_text = nullptr;
                        if (auto pub_date = xml_item->FirstChildElement("pubDate"); pub_date) {
                            date_text = pub_date->GetText();
                        }
                        else if (auto dc_date = xml_item->FirstChildElement("dc:date"); dc_date) {
                            date_text = dc_date->GetText();
                        }
                        else if (auto date = xml_item->FirstChildElement("date"); date) {
                            date_text = date->GetText();
                        }
                        else if (auto iso_date = xml_item->FirstChildElement("iso:date"); iso_date) {
                            date_text = iso_date->GetText();
                        }
                        
                        // Use our robust date parser
                        if (date_text) {
                            new_item.updated = parse_date(date_text);
                        } else {
                            // If no date found, use current time
                            new_item.updated = std::chrono::system_clock::now();
                        }
                        items.emplace_back(std::move(new_item));
                    }
                }
            }
            else if (auto root_feed = doc.FirstChildElement("feed"); root_feed) {
                auto title = root_feed->FirstChildElement("title");
                if (title) {
                    feed_title = title->GetText();
                }
                auto link = root_feed->FirstChildElement("link");
                if (link) {
                    feed_link = link->Attribute("href");
                }
                auto description = root_feed->FirstChildElement("description");
                if (description) {
                    feed_description = description->GetText();
                }
                auto image = root_feed->FirstChildElement("image");
                if (image) {
                    auto url = image->FirstChildElement("url");
                    if (url) {
                        set_image(url->GetText());
                    }
                }
                else if (auto itunes_image = root_feed->FirstChildElement("itunes:image"); itunes_image) {
                    set_image(itunes_image->Attribute("href"));
                }
                else if (auto image_el = root_feed->FirstChildElement("media:thumbnail"); image_el) {
                    set_image(image_el->Attribute("url"));
                }
                else if (auto icon_el = root_feed->FirstChildElement("icon"); icon_el) {
                    set_image(icon_el->GetText());
                }
                
                for (
                    auto xml_item = root_feed->FirstChildElement("entry"); 
                    xml_item; 
                    xml_item = xml_item->NextSiblingElement("entry")) 
                {
                    item new_item;
                    title = xml_item->FirstChildElement("title");
                    if (title) {
                        new_item.title = title->GetText();
                    }
                    link = xml_item->FirstChildElement("link");
                    if (link) {
                        new_item.link = link->Attribute("href");
                    }
                    description = xml_item->FirstChildElement("summary");
                    if (description) {
                        new_item.description = description->GetText();
                    }
                    else if (description = xml_item->FirstChildElement("content"); description) {
                        new_item.description = description->GetText();
                    }
                    // look for a media:group tag
                    if (auto media_group = xml_item->FirstChildElement("media:group"); media_group) {
                        if (auto media_content = media_group->FirstChildElement("media:content"); media_content) {
                            new_item.enclosure = media_content->Attribute("url");
                        }
                        // look for a media:thumbnail tag
                        if (auto media_thumbnail = media_group->FirstChildElement("media:thumbnail"); media_thumbnail) {
                            new_item.image_url = media_thumbnail->Attribute("url");
                            // if the feed doesn't have an image, asign the first thumbnail found
                            if (feed_image_url.empty()) {
                                set_image(new_item.image_url);
                            }
                        }
                    }
                    // Try various date formats in priority order
                    const char* date_text = nullptr;
                    if (auto updated = xml_item->FirstChildElement("updated"); updated) {
                        date_text = updated->GetText();
                    }
                    else if (auto published = xml_item->FirstChildElement("published"); published) {
                        date_text = published->GetText();
                    }
                    else if (auto created = xml_item->FirstChildElement("created"); created) {
                        date_text = created->GetText();
                    }
                    else if (auto issued = xml_item->FirstChildElement("issued"); issued) {
                        date_text = issued->GetText();
                    }
                    else if (auto modified = xml_item->FirstChildElement("modified"); modified) {
                        date_text = modified->GetText();
                    }
                    
                    // Use our robust date parser
                    if (date_text) {
                        new_item.updated = parse_date(date_text);
                    } else {
                        // If no date found, use current time
                        new_item.updated = std::chrono::system_clock::now();
                    }
                    items.emplace_back(std::move(new_item));
                }
            }
        }

        std::string source_link;
        std::string contents;
        std::string feed_title;
        std::string feed_link;
        std::string feed_description;
        std::vector<item> items;
        std::function<std::string(std::string_view)> system_runner_;
        long long repo_id;
        std::set<std::string> tags;

    private:
        std::string feed_image_url;
    };
}