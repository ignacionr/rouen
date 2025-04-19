#pragma once

#include <memory>
#include <string>
#include <stdexcept>
#include <unordered_map>

#include <SDL2/SDL.h>
#include "git.hpp"
#include "menu.hpp"
#include "fs-directory.hpp"

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
            };
            return dictionary;
        }
    };
}