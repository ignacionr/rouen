#pragma once

#include <string>
#include <vector>
#include <functional>

struct SDL_Window;

namespace rouen::platform {
#if defined(__APPLE__)
    void disable_mac_cmd_w_menu_item();
    int get_mac_titlebar_height(SDL_Window* window);
    float get_mac_backing_scale_factor(SDL_Window* window);
#else
    inline void disable_mac_cmd_w_menu_item() {}
    inline int get_mac_titlebar_height(SDL_Window* /*window*/) { return 0; }
    inline float get_mac_backing_scale_factor(SDL_Window* /*window*/) { return 1.0f; }
#endif
}

namespace rouen::mac_menu {
    struct item {
        std::string label;
        std::function<void()> action;
        bool enabled{true};
        bool is_separator{false};
        std::vector<item> sub_items;
    };

#if defined(__APPLE__)
    bool show_native_context_menu(const std::vector<item>& items, SDL_Window* window, float x, float y);
    void clear_menu_targets();
#else
    inline bool show_native_context_menu(const std::vector<item>& /*items*/, SDL_Window* /*window*/, float /*x*/, float /*y*/) { return false; }
    inline void clear_menu_targets() {}
#endif
}
