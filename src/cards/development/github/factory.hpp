#pragma once

#include "github_card.hpp"
#include "ci_card.hpp"

namespace rouen::cards::github {
    
    // Factory functions for creating GitHub-related cards
    inline std::unique_ptr<card> create_github_card(std::string_view config_name = "default") {
        return std::make_unique<github_card>(config_name);
    }
    
    inline std::unique_ptr<card> create_github_ci_card(std::string_view config_name = "default") {
        return std::make_unique<github_ci_card>(config_name);
    }

} // namespace rouen::cards::github
