#pragma once

struct SDL_Window;

namespace rouen::platform {
    void disable_mac_cmd_w_menu_item();
    int get_mac_titlebar_height(SDL_Window* window);
    float get_mac_backing_scale_factor(SDL_Window* window);
}
