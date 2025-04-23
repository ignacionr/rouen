#pragma once

#include <memory>
#include <string>
#include <stdexcept>
#include <unordered_map>

#include <SDL2/SDL.h>
#include "card.hpp"
#include "../development/git.hpp"
#include "menu.hpp"
#include "../productivity/pomodoro.hpp"
#include "../development/fs-directory.hpp"
#include "../system/sysinfo.hpp"
#include "../information/grok.hpp"
#include "../media/radio.hpp"
#include "../system/envvars.hpp"
#include "../information/rss.hpp"
#include "../information/rss_feed.hpp"
#include "../information/rss_item.hpp"
#include "../information/mail/mail.hpp"
#include "../information/calendar/calendar.hpp"
#include "../information/travel.hpp"
#include "../information/travel_plan.hpp"

namespace rouen::cards {
    struct factory {
        using factory_t = std::function<card::ptr(std::string_view, SDL_Renderer*)>;

        card::ptr create_card(std::string_view uri, SDL_Renderer* renderer) {
            std::string_view schema;
            std::string_view locator;
    
            auto colon_pos = uri.find(':');
            if (colon_pos != std::string_view::npos) {
                schema = uri.substr(0, colon_pos);
                locator = uri.substr(colon_pos + 1);
            } else {
                schema = uri;
            }
            auto factory = dictionary().find(std::string(schema));
            if (factory == dictionary().end()) {
                throw std::runtime_error("Unknown card type: " + std::string(schema));
            }
            auto card_ptr = factory->second(std::string(locator), renderer);
            if (!card_ptr) {
                throw std::runtime_error("Failed to create card: " + std::string(schema));
            }
            return card_ptr;
        }

        static std::unordered_map<std::string, factory_t> const& dictionary() {
            static std::unordered_map<std::string, factory_t> dictionary {
                {"git", [](std::string_view uri, SDL_Renderer* renderer) {
                    return std::make_shared<git>();
                }},
                {"menu", [](std::string_view uri, SDL_Renderer* renderer) {
                    return std::make_shared<menu>();
                }},
                {"dir", [](std::string_view uri, SDL_Renderer* renderer) {
                    return std::make_shared<fs_directory>(uri);
                }},
                {"pomodoro", [](std::string_view uri, SDL_Renderer* renderer) {
                    return std::make_shared<pomodoro>();
                }},
                {"sysinfo", [](std::string_view uri, SDL_Renderer* renderer) {
                    return std::make_shared<sysinfo_card>();
                }},
                {"grok", [](std::string_view uri, SDL_Renderer* renderer) {
                    return std::make_shared<grok>();
                }},
                {"radio", [](std::string_view uri, SDL_Renderer* renderer) {
                    return std::make_shared<radio>();
                }},
                {"envvars", [](std::string_view uri, SDL_Renderer* renderer) {
                    return std::make_shared<envvars_card>();
                }},
                {"rss", [](std::string_view uri, SDL_Renderer* renderer) {
                    return std::make_shared<rss>();
                }},
                {"rss-feed", [](std::string_view uri, SDL_Renderer* renderer) {
                    auto feed = std::make_shared<rss_feed>(std::string(uri));
                    if (renderer) {
                        feed->set_renderer(renderer);
                    }
                    return feed;
                }},
                {"rss-item", [](std::string_view uri, SDL_Renderer* renderer) {
                    return std::make_shared<rss_item>(std::string(uri));
                }},
                {"calendar", [](std::string_view uri, SDL_Renderer* renderer) {
                    if (uri.empty()) {
                        // Use default URL from environment variable
                        return std::make_shared<calendar>();
                    } else {
                        // Use provided URL
                        return std::make_shared<calendar>(std::string(uri));
                    }
                }},
                {"mail", [](std::string_view uri, SDL_Renderer* renderer) {
                    if (uri.empty()) {
                        // Use default credentials from environment
                        return std::make_shared<mail>();
                    } else {
                        // Parse the URI format mail:imaps://host:username:password
                        auto params = std::string(uri);
                        size_t pos1 = params.find(':');
                        if (pos1 == std::string::npos) {
                            throw std::runtime_error("Invalid mail URI format");
                        }
                        
                        std::string host = params.substr(0, pos1);
                        
                        size_t pos2 = params.find(':', pos1 + 1);
                        if (pos2 == std::string::npos) {
                            throw std::runtime_error("Invalid mail URI format");
                        }
                        
                        std::string username = params.substr(pos1 + 1, pos2 - pos1 - 1);
                        std::string password = params.substr(pos2 + 1);
                        
                        return std::make_shared<mail>(host, username, password);
                    }
                }},
                {"travel", [](std::string_view uri, SDL_Renderer* renderer) {
                    return std::make_shared<travel>();
                }},
                {"travel-plan", [](std::string_view uri, SDL_Renderer* renderer) {
                    return std::make_shared<travel_plan>(uri);
                }}
            };
            return dictionary;
        }
    };
}