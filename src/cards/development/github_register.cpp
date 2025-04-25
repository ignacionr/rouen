#include "../interface/factory.hpp"
#include "github/github_card.hpp"

namespace rouen::cards {
    void register_github_card(std::unordered_map<std::string, factory::factory_t>& dictionary) {
        dictionary["github"] = [](std::string_view uri, SDL_Renderer*) {
            if (uri.empty()) {
                return std::make_shared<github_card>();
            } else {
                return std::make_shared<github_card>(uri);
            }
        };
    }
}