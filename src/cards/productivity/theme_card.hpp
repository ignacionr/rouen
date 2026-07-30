#pragma once

#include "../interface/card.hpp"
#include "../../helpers/theme_manager.hpp"
#include "../../external/IconsMaterialDesign.h"
#include <array>
#include <string>

namespace rouen::cards {

    class theme_card : public card {
    public:
        theme_card();

        std::string get_uri() const override;

        bool render() override;

    private:
        char new_theme_name_[128];
    };

} // namespace rouen::cards
