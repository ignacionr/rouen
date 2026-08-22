#pragma once

// 1. Standard includes in alphabetic order
#include <string>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
// None in this file

// 3. All other includes
// None in this file

// Kept as its own header (rather than folded into factory.hpp) so that
// menu.hpp can read the registered entries without creating a circular
// include: factory.hpp already includes menu.hpp to know about the
// built-in "menu" card.
namespace rouen::cards {

    struct plugin_menu_entry {
        std::string schema;
        std::string display_name;
    };

    inline std::vector<plugin_menu_entry>& plugin_menu_entries() {
        static std::vector<plugin_menu_entry> instance;
        return instance;
    }

} // namespace rouen::cards
