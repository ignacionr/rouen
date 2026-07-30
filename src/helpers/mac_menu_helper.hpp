#pragma once

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
