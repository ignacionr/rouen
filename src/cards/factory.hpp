#pragma once

#include <memory>

#include "git.hpp"

namespace rouen::cards {
    struct factory {
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

            if (schema == "git") {
                return std::make_shared<git>(renderer);
            }
            return nullptr;
        }
    };
}