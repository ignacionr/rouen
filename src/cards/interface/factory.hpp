#pragma once

// 1. Standard includes in alphabetic order
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

// 2. Libraries used in the project, in alphabetic order
#include <SDL3/SDL.h>

// 3. All other includes
#include "card.hpp"
#include "menu.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/media_player_alarm.hpp"
#include "../../helpers/media_player.hpp"
#include "../../hosts/video_feed_host.hpp"
#include "../development/cmake.hpp"
#include "../development/fs-directory.hpp"
#include "../development/git.hpp"
#include "../development/github.hpp"
#include "../information/calendar/calendar.hpp"
#include "../information/adaptive_card.hpp"
#include "../information/ai_chat.hpp"
#include "../information/directory_card.hpp"
#include "../information/contact_card.hpp"
#include "../information/mail/mail.hpp"
#include "../information/markdown_notes.hpp"
#include "../information/pdf_viewer.hpp"
#include "../information/image_viewer.hpp"
#include "../information/rss.hpp"
#include "../information/rss_feed.hpp"
#include "../information/rss_item.hpp"
#include "../information/rss_smart_list.hpp"
#include "../information/travel.hpp"
#include "../information/travel_plan.hpp"
#include "../information/weather.hpp"
#include "../information/whatsapp.hpp"
#include "../information/wikipedia.hpp"
#include "../information/number_series.hpp"
#include "../media/chess_replay.hpp"
#include "../media/radio.hpp"
#include "../media/radiocut.hpp"
#include "../media/youtube_search.hpp"
#include "../media/camera.hpp"
#include "../media/media_companion.hpp"
#include "../media/media_card.hpp"
#include "../productivity/alarm.hpp"
#include "../productivity/converter.hpp"
#include "../productivity/jira_card.hpp"
#include "../productivity/pomodoro.hpp"
#include "../productivity/objectives_card.hpp"
#include "../productivity/trello_card.hpp"
#include "../productivity/kpi_card.hpp"
#include "../productivity/theme_card.hpp"
#include "../productivity/invoice_card.hpp"
#include "../system/about.hpp"
#include "../system/cast_control.hpp"
#include "../system/dbrepair.hpp"
#include "../system/display_card.hpp"
#include "../system/envvars.hpp"
#include "../system/notifications.hpp"
#include "../system/settings.hpp"
#include "../system/sync_card.hpp"
#include "../system/subnet_scanner.hpp"
#include "../system/sysinfo.hpp"
#include "../system/terminal.hpp"

// Forward declare GitHub card to avoid circular dependency
namespace rouen::cards {
    struct github_card;
}

namespace rouen::cards {
    struct factory {
        using factory_t = std::function<card::ptr(std::string_view, SDL_Renderer*)>;

        static card::ptr create_card(std::string_view uri, SDL_Renderer* renderer) {
            std::string_view schema;
            std::string_view locator;
    
            auto colon_pos = uri.find(':');
            if (colon_pos != std::string_view::npos) {
                schema = uri.substr(0, colon_pos);
                locator = uri.substr(colon_pos + 1);
            } else {
                schema = uri;
            }
            auto factory_it = dictionary().find(std::string(schema));
            if (factory_it == dictionary().end()) {
                throw std::runtime_error("Unknown card type: " + std::string(schema));
            }
            auto card_ptr = factory_it->second(std::string(locator), renderer);
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
                instance.emplace("git", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<git>(uri);
                });

                instance.emplace("github", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<github_card>(uri);
                });

                instance.emplace("github-ci", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<rouen::cards::github::github_ci_card>(uri);
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
                
                instance.emplace("objectives", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<objectives_card>();
                });
                
                instance.emplace("alarm", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<alarm>(uri);
                });
                
                instance.emplace("converter", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<converter>();
                });
                
                instance.emplace("sysinfo", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<sysinfo_card>();
                });
                
                instance.emplace("subnet-scanner", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<subnet_scanner>();
                });
                
                instance.emplace("ai_chat", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<ai_chat>(::helpers::StringHelper::url_decode(uri));
                });
                
                instance.emplace("ai-chat", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<ai_chat>(::helpers::StringHelper::url_decode(uri));
                });
                
                instance.emplace("radio", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<radio>();
                });
                
                instance.emplace("radiocut", [](std::string_view, SDL_Renderer* renderer) {
                    auto card = std::make_shared<radiocut>();
                    if (renderer) {
                        card->set_renderer(renderer);
                    }
                    return card;
                });

                instance.emplace("youtube", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<youtube_search>(uri);
                });

                instance.emplace("media-companion", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<media_companion>();
                });

                instance.emplace("media_companion", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<media_companion>();
                });
                
                instance.emplace("envvars", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<envvars_card>();
                });
                
                instance.emplace("display", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<display_card>();
                });

                instance.emplace("settings", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<settings_card>();
                });

                instance.emplace("sync", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<sync_card>();
                });

                instance.emplace("cast-control", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<cast_control>();
                });

                instance.emplace("cast-start", [](std::string_view, SDL_Renderer*) {
                    auto h = rouen::hosts::VideoFeedHost::get_host();
                    if (h) h->start();
                    return std::make_shared<menu>();
                });

                instance.emplace("media-play", [](std::string_view locator, SDL_Renderer*) {
                    std::string url = ::helpers::StringHelper::url_decode(locator);
                    auto& item = media_player::get_item(url);
                    item.url = url;
                    item.playMedia();
                    return std::make_shared<menu>();
                });

                instance.emplace("cast-play", [](std::string_view locator, SDL_Renderer*) {
                    auto h = rouen::hosts::VideoFeedHost::get_host();
                    if (h) h->start();
                    std::string url = ::helpers::StringHelper::url_decode(locator);
                    if (!url.empty()) {
                        auto& item = media_player::get_item(url);
                        item.url = url;
                        item.playMedia();
                    }
                    return std::make_shared<menu>();
                });

                instance.emplace("notifications", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<notifications_card>();
                });

                instance.emplace("camera", [](std::string_view locator, SDL_Renderer*) {
                    auto card_inst = std::make_shared<camera_card>();
                    if (!locator.empty()) {
                        card_inst->handle_uri(locator);
                    }
                    return card_inst;
                });
                
                instance.emplace("kpis", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<kpi_card>();
                });
                
                instance.emplace("about", [](std::string_view, SDL_Renderer* renderer) {
                    return std::make_shared<about_card>(renderer);
                });
                
                // Register the new terminal card
                instance.emplace("terminal", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<terminal>(uri);
                });
                
                instance.emplace("rss", [](std::string_view, SDL_Renderer* renderer) {
                    auto card = std::make_shared<rss>();
                    if (renderer) {
                        card->set_renderer(renderer);
                    }
                    return card;
                });
                
                instance.emplace("rss-feed", [](std::string_view uri, SDL_Renderer* renderer) {
                    auto feed = std::make_shared<rss_feed>(std::string(uri));
                    if (renderer) {
                        feed->set_renderer(renderer);
                    }
                    return feed;
                });
                
                instance.emplace("rss-item", [](std::string_view uri, SDL_Renderer* renderer) {
                    auto card = std::make_shared<rss_item>(std::string(uri));
                    if (renderer) {
                        card->set_renderer(renderer);
                    }
                    return card;
                });
                
                instance.emplace("rss-smart-list", [](std::string_view uri, SDL_Renderer* renderer) {
                    auto card = std::make_shared<rss_smart_list>(std::string(uri));
                    if (renderer) {
                        card->set_renderer(renderer);
                    }
                    return card;
                });
                
                instance.emplace("calendar", [](std::string_view uri, SDL_Renderer*) {
                    if (uri.empty()) {
                        // Use default URL from environment variable
                        return std::make_shared<calendar>();
                    }
                    // Use provided URL
                    return std::make_shared<calendar>(std::string(uri));
                });

                instance.emplace("adaptive-card", [](std::string_view locator, SDL_Renderer*) {
                    return std::make_shared<adaptive_card>(locator);
                });

                instance.emplace("directory", [](std::string_view locator, SDL_Renderer* renderer) {
                    auto card = std::make_shared<directory_card>(locator);
                    if (renderer) card->set_renderer(renderer);
                    return card;
                });

                instance.emplace("contacts", [](std::string_view locator, SDL_Renderer* renderer) {
                    auto card = std::make_shared<directory_card>(locator);
                    if (renderer) card->set_renderer(renderer);
                    return card;
                });

                instance.emplace("contact", [](std::string_view locator, SDL_Renderer* renderer) {
                    auto card = std::make_shared<contact_card>(locator);
                    if (renderer) card->set_renderer(renderer);
                    return card;
                });
                
                instance.emplace("number-series", [](std::string_view locator, SDL_Renderer*) {
                    return std::make_shared<number_series_card>(locator);
                });
                
                instance.emplace("mail", [](std::string_view uri, SDL_Renderer*) {
                    if (uri.empty()) {
                        // Use default credentials from environment
                        return std::make_shared<mail>();
                    }
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

                instance.emplace("whatsapp", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<whatsapp>(uri);
                });

                instance.emplace("wikipedia", [](std::string_view uri, SDL_Renderer* renderer) {
                    auto card = std::make_shared<wikipedia>(uri);
                    if (renderer) {
                        card->set_renderer(renderer);
                    }
                    return card;
                });

                instance.emplace("notes", [](std::string_view locator, SDL_Renderer*) {
                    return std::make_shared<markdown_notes>(locator);
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
                
                // Register Trello cards
                instance.emplace("trello", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<trello_card>();
                });
                
                instance.emplace("trello-board", [](std::string_view board_id, SDL_Renderer*) {
                    return std::make_shared<trello_card>(std::string(board_id));
                });
                
                // Register the chess replay card
                instance.emplace("chess", [](std::string_view pgn_path, SDL_Renderer*) {
                    return std::make_shared<chess_replay>(pgn_path);
                });

                // Register the theme settings card
                instance.emplace("theme", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<theme_card>();
                });

                // Register the PDF viewer card
                instance.emplace("pdf", [](std::string_view uri, SDL_Renderer* renderer) {
                    auto card = std::make_shared<pdf_viewer>(uri);
                    if (renderer) {
                        card->set_renderer(renderer);
                    }
                    return card;
                });

                // Register the Image viewer card
                instance.emplace("image", [](std::string_view uri, SDL_Renderer* renderer) {
                    auto card = std::make_shared<image_viewer>(uri);
                    if (renderer) {
                        card->set_renderer(renderer);
                    }
                    return card;
                });

                instance.emplace("img", [](std::string_view uri, SDL_Renderer* renderer) {
                    auto card = std::make_shared<image_viewer>(uri);
                    if (renderer) {
                        card->set_renderer(renderer);
                    }
                    return card;
                });

                // Register the Media player card
                instance.emplace("media", [](std::string_view uri, SDL_Renderer*) {
                    return std::make_shared<media_card>(uri);
                });

                // Register the Invoice card
                instance.emplace("invoice", [](std::string_view, SDL_Renderer*) {
                    return std::make_shared<invoice_card>();
                });

                initialized = true;
            }
            
            return instance;
        }
    };
}
