#pragma once

#include <string>
#include <vector>

struct card;

namespace rouen::helpers {

class card_decorations {
public:
    struct menu_item_info {
        std::string uri;
        std::string title;
    };

    // Renders window decorations for the specified card (e.g. "Add to Menu" icon button)
    static void render_decorations(card* c);

    // Checks if the given URI is tagged with ".menu"
    static bool is_added_to_menu(const std::string& uri);

    // Toggles the ".menu" tag for the given card (saving/removing title in tag_manager)
    static void toggle_menu_tag(card* c);

    // Retrieves all items tagged with ".menu" from tag_manager
    static std::vector<menu_item_info> get_menu_tagged_items();
};

} // namespace rouen::helpers
