#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

#include "../interface/card.hpp"
#include "../../helpers/theme_manager.hpp"
#include "../../external/IconsMaterialDesign.h"

namespace rouen::cards {

    class theme_card : public card {
    public:
        theme_card();

        std::string get_uri() const override;

        std::string get_adaptive_card_json() const override;
        void handle_action(std::string_view action_json) override;

        bool render() override;

    private:
        char new_theme_name_[128];
    };

} // namespace rouen::cards
