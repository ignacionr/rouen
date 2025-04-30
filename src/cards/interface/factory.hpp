#pragma once

// 1. Standard includes in alphabetic order
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

// 2. Libraries used in the project, in alphabetic order
#include <SDL.h>

// 3. All other includes
#include "card.hpp"
#include "menu.hpp"
#include "../development/cmake.hpp"
#include "../development/fs-directory.hpp"
#include "../development/git.hpp"
#include "../information/calendar/calendar.hpp"
#include "../information/grok.hpp"
#include "../information/mail/mail.hpp"
#include "../information/rss.hpp"
#include "../information/rss_feed.hpp"
#include "../information/rss_item.hpp"
#include "../information/travel.hpp"
#include "../information/travel_plan.hpp"
#include "../information/weather.hpp"
#include "../media/radio.hpp"
#include "../productivity/alarm.hpp"
#include "../productivity/jira_card.hpp"
#include "../productivity/pomodoro.hpp"
#include "../system/dbrepair.hpp"
#include "../system/envvars.hpp"
#include "../system/subnet_scanner.hpp"
#include "../system/sysinfo.hpp"
#include "../system/terminal.hpp"

// Forward declare GitHub card to avoid circular dependency
namespace rouen::cards {
    struct github_card;
    class dummy_jira_card; // Forward declaration for the dummy Jira card
}

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
            static std::unordered_map<std::string, factory_t> instance;
            
            // Initialize the dictionary only once
            static bool initialized = false;
            if (!initialized) {
                // Add card factories
                instance.emplace("git", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<git>();
                });
                
                instance.emplace("menu", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<menu>();
                });
                
                instance.emplace("dir", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<fs_directory>(uri);
                });
                
                instance.emplace("cmake", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<cmake_card>(uri);
                });
                
                instance.emplace("pomodoro", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<pomodoro>();
                });
                
                instance.emplace("alarm", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<alarm>(uri);
                });
                
                instance.emplace("sysinfo", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<sysinfo_card>();
                });
                
                instance.emplace("subnet-scanner", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<subnet_scanner>();
                });
                
                instance.emplace("grok", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<grok>();
                });
                
                instance.emplace("radio", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<radio>();
                });
                
                instance.emplace("envvars", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<envvars_card>();
                });
                
                // Register the new terminal card
                instance.emplace("terminal", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<terminal>(uri);
                });
                
                instance.emplace("rss", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<rss>();
                });
                
                instance.emplace("rss-feed", [](std::string_view uri, SDL_Renderer* renderer) {
                    auto feed = std::make_shared<rss_feed>(std::string(uri));
                    if (renderer) {
                        feed->set_renderer(renderer);
                    }
                    return feed;
                });
                
                instance.emplace("rss-item", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<rss_item>(std::string(uri));
                });
                
                instance.emplace("calendar", [](std::string_view uri, SDL_Renderer*) {
                    if (uri.empty()) {
                        // Use default URL from environment variable
                        return std::make_shared<calendar>();
                    } else {
                        // Use provided URL
                        return std::make_shared<calendar>(std::string(uri));
                    }
                });
                
                instance.emplace("mail", [](std::string_view uri, SDL_Renderer*) {
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
                        pos1 = params.find(':', pos1 + 1);
                        if (pos1 == std::string::npos) {
                            throw std::runtime_error("Invalid mail URI format");
                        }
                        
                        std::string host = params.substr(0, pos1);
                        
                        size_t pos2 = params.find(':', pos1 + 1);
                        
                        std::string username = params.substr(pos1 + 1, pos2 - pos1 - 1);
                        std::string password = (pos2 == std::string::npos) ? std::string{} : params.substr(pos2 + 1);
                        
                        return std::make_shared<mail>(host, username, password);
                    }
                });
                
                instance.emplace("travel", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<travel>();
                });
                
                instance.emplace("travel-plan", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<travel_plan>(uri);
                });
                
                instance.emplace("dbrepair", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<dbrepair_card>();
                });
                
                instance.emplace("weather", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<weather>(uri);
                });
                
                // Explicitly register Jira cards here rather than relying solely on the registrar
                instance.emplace("jira", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<jira_card>();
                });

                instance.emplace("jira-projects", [](std::string_view, SDL_Renderer*) {
                    auto card = std::make_shared<jira_card>();
                    return card;
                });

                instance.emplace("jira-search", [](std::string_view, SDL_Renderer*) {
                    auto card = std::make_shared<jira_card>();
                    return card;
                });

                initialized = true;
            }
            
            return instance;
        }
    };
}