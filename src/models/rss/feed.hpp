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

#include "tinyxml2.h"
#include "../../helpers/fetch.hpp"
#include "../../helpers/html_media_extractor.hpp"
#include "../../registrar.hpp"
#include "feed_item.hpp"
#include "rss_date_parser.hpp"
#include "feed_xml_parser.hpp"

namespace media::rss {
    struct feed {
        void operator()(std::string_view partial_contents) {
            contents += partial_contents;
            
            // Fast path: avoid parsing on every chunk if it doesn't end with a feed closing tag
            std::string_view trimmed(contents);
            size_t last_non_ws = trimmed.find_last_not_of(" \t\r\n");
            if (last_non_ws == std::string_view::npos) {
                return;
            }
            trimmed = trimmed.substr(0, last_non_ws + 1);
            
            auto ends_with_case_insensitive = [](std::string_view str, std::string_view suffix) {
                if (str.size() < suffix.size()) return false;
                std::string_view part = str.substr(str.size() - suffix.size());
                for (size_t i = 0; i < suffix.size(); ++i) {
                    if (std::tolower(static_cast<unsigned char>(part[i])) != 
                        std::tolower(static_cast<unsigned char>(suffix[i]))) {
                        return false;
                    }
                }
                return true;
            };

            bool complete = false;
            if (ends_with_case_insensitive(trimmed, "</rss>") || 
                ends_with_case_insensitive(trimmed, "</feed>") || 
                ends_with_case_insensitive(trimmed, "</rdf>")) {
                complete = true;
            }
            
            if (!complete) {
                return;
            }

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

        void operator()(tinyxml2::XMLDocument const &doc) {
            if (auto root = doc.FirstChildElement("rss"); root) {
                auto channel = root->FirstChildElement("channel");
                if (channel) {
                    auto title = channel->FirstChildElement("title");
                    if (title && title->GetText()) {
                        feed_title = title->GetText();
                    }
                    auto link = channel->FirstChildElement("link");
                    if (link && link->GetText()) {
                        feed_link = link->GetText();
                    }
                    auto description = channel->FirstChildElement("description");
                    if (description && description->GetText()) {
                        feed_description = description->GetText();
                    }
                    auto image = channel->FirstChildElement("image");
                    if (image) {
                        auto url = image->FirstChildElement("url");
                        if (url && url->GetText()) {
                            set_image(url->GetText());
                        }
                    }
                    else {
                        auto itunes_image = channel->FirstChildElement("itunes:image");
                        if (itunes_image && itunes_image->Attribute("href")) {
                            set_image(itunes_image->Attribute("href"));
                        }
                    }
                    for (
                        auto xml_item = channel->FirstChildElement("item");
                        xml_item;
                        xml_item = xml_item->NextSiblingElement("item")) 
                    {
                        feed_item new_item;
                        title = xml_item->FirstChildElement("title");
                        if (title && title->GetText()) {
                            new_item.title = title->GetText();
                        }
                        link = xml_item->FirstChildElement("link");
                        if (link && link->GetText()) {
                            new_item.link = link->GetText();
                        }
                        description = xml_item->FirstChildElement("description");
                        if (description && !description->NoChildren() && description->GetText()) {
                            new_item.description = description->GetText();
                        }
                        
                        // Also check for content:encoded (used by many RSS feeds like RT.com for rich content)
                        auto content_encoded = xml_item->FirstChildElement("content:encoded");
                        if (content_encoded && content_encoded->GetText()) {
                            // If we have content:encoded, prefer it over description for media extraction
                            // as it often contains the actual embedded media (videos, iframes, etc.)
                            if (new_item.description.empty()) {
                                new_item.description = content_encoded->GetText();
                            } else {
                                // Append content:encoded to description for comprehensive media extraction
                                new_item.description += "\n" + std::string(content_encoded->GetText());
                            }
                        }
                        
                        auto enclosure = xml_item->FirstChildElement("enclosure");
                        if (enclosure && enclosure->Attribute("url")) {
                            std::string enclosure_url = enclosure->Attribute("url");
                            // Filter out image URLs that are incorrectly used as enclosures (like RT.com thumbnails)
                            if (media::html::is_media_url(enclosure_url)) {
                                new_item.enclosure = enclosure_url;
                            }
                            // If it's an image URL, put it in image_url instead
                            else if (enclosure_url.find(".jpg") != std::string::npos || 
                                     enclosure_url.find(".jpeg") != std::string::npos ||
                                     enclosure_url.find(".png") != std::string::npos ||
                                     enclosure_url.find(".gif") != std::string::npos ||
                                     enclosure_url.find("thumbnail") != std::string::npos) {
                                new_item.image_url = enclosure_url;
                            }
                        }
                        // if there is no direct enclosure, we can try to get one if the link is to youtube
                        else if (new_item.link.find("youtube.com") != std::string::npos) {
                            new_item.enclosure = new_item.link;
                        }
                        // look for an itunes:image tag
                        auto itunes_image = xml_item->FirstChildElement("itunes:image");
                        if (itunes_image && itunes_image->Attribute("href")) {
                            new_item.image_url = itunes_image->Attribute("href");
                        }
                        
                        // Enhanced: Extract media URLs from description content
                        if (!new_item.description.empty()) {
                            new_item.extracted_media_urls = media::html::extract_media_urls(new_item.description);
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
                            new_item.updated = parse_rss_date(date_text);
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
                if (title && title->GetText()) {
                    feed_title = title->GetText();
                }
                auto link = root_feed->FirstChildElement("link");
                if (link && link->Attribute("href")) {
                    feed_link = link->Attribute("href");
                }
                auto description = root_feed->FirstChildElement("description");
                if (description && description->GetText()) {
                    feed_description = description->GetText();
                }
                auto image = root_feed->FirstChildElement("image");
                if (image) {
                    auto url = image->FirstChildElement("url");
                    if (url && url->GetText()) {
                        set_image(url->GetText());
                    }
                }
                else if (auto itunes_image = root_feed->FirstChildElement("itunes:image"); itunes_image && itunes_image->Attribute("href")) {
                    set_image(itunes_image->Attribute("href"));
                }
                else if (auto image_el = root_feed->FirstChildElement("media:thumbnail"); image_el && image_el->Attribute("url")) {
                    set_image(image_el->Attribute("url"));
                }
                else if (auto icon_el = root_feed->FirstChildElement("icon"); icon_el && icon_el->GetText()) {
                    set_image(icon_el->GetText());
                }
                
                for (
                    auto xml_item = root_feed->FirstChildElement("entry"); 
                    xml_item; 
                    xml_item = xml_item->NextSiblingElement("entry")) 
                {
                    feed_item new_item;
                    title = xml_item->FirstChildElement("title");
                    if (title && title->GetText()) {
                        new_item.title = title->GetText();
                    }
                    link = xml_item->FirstChildElement("link");
                    if (link && link->Attribute("href")) {
                        new_item.link = link->Attribute("href");
                    }
                    description = xml_item->FirstChildElement("summary");
                    if (description && description->GetText()) {
                        new_item.description = description->GetText();
                    }
                    else if (description = xml_item->FirstChildElement("content"); description && description->GetText()) {
                        new_item.description = description->GetText();
                    }
                    // look for a media:group tag
                    if (auto media_group = xml_item->FirstChildElement("media:group"); media_group) {
                        if (auto media_content = media_group->FirstChildElement("media:content"); media_content && media_content->Attribute("url")) {
                            std::string content_url = media_content->Attribute("url");
                            // Only use as enclosure if it's actually a media file, not an image
                            if (media::html::is_media_url(content_url)) {
                                new_item.enclosure = content_url;
                            }
                        }
                        // look for a media:thumbnail tag
                        if (auto media_thumbnail = media_group->FirstChildElement("media:thumbnail"); media_thumbnail && media_thumbnail->Attribute("url")) {
                            new_item.image_url = media_thumbnail->Attribute("url");
                            // if the feed doesn't have an image, asign the first thumbnail found
                            if (feed_image_url.empty()) {
                                set_image(new_item.image_url);
                            }
                        }
                    }
                    
                    // Enhanced: Extract media URLs from description content
                    if (!new_item.description.empty()) {
                        new_item.extracted_media_urls = media::html::extract_media_urls(new_item.description);
                    }
                    
                    // Try various date formats in priority order
                    const char* date_text = nullptr;
                    if (auto updated = xml_item->FirstChildElement("updated"); updated && updated->GetText()) {
                        date_text = updated->GetText();
                    }
                    else if (auto published = xml_item->FirstChildElement("published"); published && published->GetText()) {
                        date_text = published->GetText();
                    }
                    else if (auto created = xml_item->FirstChildElement("created"); created && created->GetText()) {
                        date_text = created->GetText();
                    }
                    else if (auto issued = xml_item->FirstChildElement("issued"); issued && issued->GetText()) {
                        date_text = issued->GetText();
                    }
                    else if (auto modified = xml_item->FirstChildElement("modified"); modified && modified->GetText()) {
                        date_text = modified->GetText();
                    }
                    
                    // Use our robust date parser
                    if (date_text) {
                        new_item.updated = parse_rss_date(date_text);
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
        std::vector<feed_item> items;
        long long repo_id;
        std::set<std::string> tags;
        bool is_permanently_redirected = false;
    private:
        std::string feed_image_url;
    };
}
