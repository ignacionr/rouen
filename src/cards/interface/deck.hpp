#pragma once

// 1. Standard includes in alphabetic order
#include <algorithm> // Added for std::find_if
#include <chrono>    // Added for timestamp
#include <cmath>     // Added for std::abs
#include <fstream>   // Added for file I/O
#include <functional>
#include <iomanip>   // Added for std::put_time
#include <iostream>  // Added for console output
#include <limits>    // Added for std::numeric_limits
#include <sstream>   // Added for string stream
#include <string>
#include <utility>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
#include "../../helpers/sdl_compat.hpp"

// 3. All other includes
#include "../../helpers/capture_helper.hpp"
#include "../../helpers/deferred_operations.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../registrar.hpp"
#include "../productivity/editor.hpp"
#include "factory.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/card_render_metrics.hpp"

struct deck {
    deck(SDL_Renderer* sdl_renderer): renderer(sdl_renderer) {
        // Initialize colors
        background_color = {0.0, 0.0f, 0.0f, 0.70f};
        editor_background_color = {0.76f, 0.76f, 0.66f, 0.40f};
        text_color = {1.0f, 1.0f, 1.0f, 1.0f};
        
        // Register to present new cards
        registrar::add<std::function<void(std::string const&)>>(
            "create_card", 
            std::make_shared<std::function<void(std::string const&)>>(
                [this](std::string const& uri) { create_card(uri); }
            )
        );

        // Register to get the active card count
        registrar::add<std::function<size_t()>>(
            "get_card_count", 
            std::make_shared<std::function<size_t()>>(
                [this]() { return cards_.size(); }
            )
        );

        // Register to get active card instances (for video feed surface painting)
        registrar::add<std::function<std::vector<std::shared_ptr<card>>()>>(
            "get_active_cards",
            std::make_shared<std::function<std::vector<std::shared_ptr<card>>()>>(
                [this]() { return cards_; }
            )
        );
        
        // Register focus and scroll services
        registrar::add<std::function<void(std::string const&)>>(
            "focus_card", 
            std::make_shared<std::function<void(std::string const&)>>(
                [this](std::string const& uri) { focus_card_by_uri(uri); }
            )
        );

        registrar::add<std::function<void(size_t)>>(
            "focus_card_index", 
            std::make_shared<std::function<void(size_t)>>(
                [this](size_t index) { focus_card_by_index(index); }
            )
        );

        registrar::add<std::function<void(int)>>(
            "scroll_to_section", 
            std::make_shared<std::function<void(int)>>(
                [this](int section) { scroll_to_section(section); }
            )
        );

        registrar::add<std::function<std::string()>>(
            "get_deck_status",
            std::make_shared<std::function<std::string()>>(
                [this]() {
                    std::string focused_title = "";
                    std::string focused_uri = "";
                    if (auto last_locked = last_focused_card_.lock()) {
                        focused_title = last_locked->window_title;
                        focused_uri = last_locked->get_uri();
                    }
                    std::string cards_json = "[";
                    for (size_t i = 0; i < cards_.size(); ++i) {
                        if (i > 0) cards_json += ",";
                        cards_json += std::format(R"({{"index":{},"title":"{}","uri":"{}"}})", i, cards_[i]->window_title, cards_[i]->get_uri());
                    }
                    cards_json += "]";
                    return std::format(R"({{"current_scroll_x":{:.1f},"target_scroll_x":{:.1f},"card_count":{},"focused_card_title":"{}","focused_card_uri":"{}","cards":{}}})",
                        current_scroll_x, target_scroll_x, cards_.size(), focused_title, focused_uri, cards_json);
                }
            )
        );
        
        // Load cards from ImGui configuration or create default menu card
        load_card_uris();
    }

    ~deck() {
        // Save card state when the deck is destroyed
        save_card_uris();
        
        // Unregister the services
        registrar::remove<std::function<void(std::string const&)>>("create_card");
        registrar::remove<std::function<size_t()>>("get_card_count");
        registrar::remove<std::function<std::vector<std::shared_ptr<card>>>>("get_active_cards");
        registrar::remove<std::function<void(std::string const&)>>("focus_card");
        registrar::remove<std::function<void(size_t)>>("focus_card_index");
        registrar::remove<std::function<void(int)>>("scroll_to_section");
        registrar::remove<std::function<std::string()>>("get_deck_status");
    }

    void create_card(std::string_view uri, bool move_first = false) {
        // this needs to be deferred
        auto deferred_ops = registrar::get<deferred_operations>("deferred_ops");
        deferred_ops->queue([this, uri_str = std::string{uri}, move_first] {
            create_card_impl(uri_str, move_first);
        });
    }

    void focus_card_by_uri(std::string_view uri) {
        create_card(uri);
    }

    void focus_card_by_index(size_t index) {
        auto deferred_ops = registrar::get<deferred_operations>("deferred_ops");
        if (deferred_ops) {
            deferred_ops->queue([this, index] {
                if (index < cards_.size()) {
                    cards_[index]->grab_focus = true;
                    last_focused_card_ = cards_[index];
                }
            });
        }
    }

    void scroll_to_section(int section_idx) {
        auto deferred_ops = registrar::get<deferred_operations>("deferred_ops");
        if (deferred_ops) {
            deferred_ops->queue([this, section_idx] {
                float const section_width = std::max(ImGui::GetMainViewport()->Size.x, 1.0f);
                target_scroll_x = static_cast<float>(section_idx) * section_width;
            });
        }
    }

    void create_card_impl(std::string_view uri, bool move_first = false) {
        auto existing_card = std::find_if(cards_.begin(), cards_.end(),
            [&uri](const auto& card) { return card->matches_uri(uri); });
        if (existing_card != cards_.end()) {
            // If the card already exists, we will make it request the focus and handle the URI
            (*existing_card)->handle_uri(uri);
            (*existing_card)->grab_focus = true;
            last_focused_card_ = *existing_card;
        }
        else {
            static auto card_factory {rouen::cards::factory()};
            auto card_ptr = card_factory.create_card(uri, renderer);
            if (card_ptr) {
                // Register MCP functions for the new card
                card_ptr->register_mcp_functions();
                card_ptr->grab_focus = true;
                
                if (move_first) {
                    // Move the card to the front of the vector
                    cards_.insert(cards_.begin(), card_ptr);
                } else {
                    // Insert the new card next to the last focused card or currently focused card
                    auto target_it = cards_.end();
                    if (auto last_focused = last_focused_card_.lock()) {
                        target_it = std::find(cards_.begin(), cards_.end(), last_focused);
                    }

                    if (target_it == cards_.end()) {
                        target_it = std::find_if(cards_.begin(), cards_.end(),
                            [](const auto& card) { return card->is_focused; });
                    }

                    if (target_it != cards_.end()) {
                        cards_.insert(target_it + 1, card_ptr);
                    } else {
                        // Add the card to the end of the vector if no card is currently focused
                        cards_.push_back(card_ptr);
                    }
                }
                last_focused_card_ = card_ptr;
            }
        }
    }

    struct color_setup {
        color_setup(ImVec4 first_color, ImVec4 second_color) {
            // Push first color elements
            for (const auto& col : first_color_elements) {
                ImGui::PushStyleColor(col, first_color);
            }
            
            // Push second color elements
            for (const auto& col : second_color_elements) {
                ImGui::PushStyleColor(col, second_color);
            }
    
        }
        ~color_setup() {
            // Pop all style colors (2 initial + size of both arrays)
            const int total_style_pushes = std::size(first_color_elements) + std::size(second_color_elements);
            for (int i = 0; i < total_style_pushes; ++i) {
                ImGui::PopStyleColor();
            }
        }
        static constexpr ImGuiCol_ first_color_elements[] = {
            ImGuiCol_TitleBgActive,
            ImGuiCol_Border,
            ImGuiCol_BorderShadow,
            ImGuiCol_ButtonHovered,
            ImGuiCol_CheckMark,
        };
        
        static constexpr ImGuiCol_ second_color_elements[] = {
            ImGuiCol_TitleBgCollapsed,
            ImGuiCol_Button,
            ImGuiCol_ButtonActive,
            ImGuiCol_FrameBg,
            ImGuiCol_FrameBgHovered,
            ImGuiCol_FrameBgActive,
            ImGuiCol_ResizeGrip,
            ImGuiCol_ResizeGripHovered,
            ImGuiCol_ResizeGripActive,
            ImGuiCol_SliderGrab,
            ImGuiCol_SliderGrabActive,
            ImGuiCol_Separator,
            ImGuiCol_SeparatorHovered,
            ImGuiCol_SeparatorActive,
            ImGuiCol_Tab,
            ImGuiCol_TabHovered,
            ImGuiCol_TabActive,
            ImGuiCol_TabUnfocused,
            ImGuiCol_TabUnfocusedActive,
            ImGuiCol_MenuBarBg,
            ImGuiCol_PopupBg,
            ImGuiCol_HeaderHovered,
            ImGuiCol_HeaderActive
        };
    };

    float get_width_factor() const {
        try {
            auto get_wf = registrar::get<std::function<float()>>("get_width_factor");
            if (get_wf) {
                return (*get_wf)();
            }
        } catch (...) {}
        return 4.0f;
    }

    bool render(card &c, float &x, float height, int &requested_fps, float y = 0.0f, float override_width = -1.0f) {
        float const scaled_width = (override_width > 0.0f) ? override_width : c.width;
        ImGui::SetNextWindowPos({x, y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({scaled_width, height}, ImGuiCond_Always);

        color_setup colors(c.get_color(0), c.get_color(1));

        if (c.grab_focus) {
            ImGui::SetNextWindowFocus();
        }
        ui_context_.prepare();
        auto t0 = std::chrono::high_resolution_clock::now();
        bool result = c.render(ui_context_);
        if (c.is_focused) {
            c.grab_focus = false;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::string card_name = c.window_title;
        size_t hash_pos = card_name.find("##");
        if (hash_pos != std::string::npos) {
            card_name = card_name.substr(0, hash_pos);
        }

        if (duration_ms > 100.0) {
            std::cerr << "[DECK SLOW RENDER] Card '" << card_name << "' (" << c.get_uri() << ") took " << duration_ms << " ms to render!" << std::endl;
        }
        rouen::helpers::CardRenderMetrics::instance().record(card_name, c.get_uri(), duration_ms);

        requested_fps = std::max(requested_fps, c.requested_fps);
        
        x += scaled_width + 2.0f;
        return result;
    }

    void handle_shortcuts() {
        auto& io = ImGui::GetIO();
        bool ctrl = io.KeyCtrl || io.KeySuper;
        if (ctrl) {
            if (io.KeyShift) {
                if (ImGui::IsKeyPressed(ImGuiKey_P)) {
                    // Handle Ctrl+Shift+P shortcut
                    // Check if a menu card already exists
                    auto menu_card = std::find_if(cards_.begin(), cards_.end(), 
                        [](const auto& card) { 
                            // Check if the window title contains "Menu"
                            return card->window_title.find("Menu") != std::string::npos; 
                        });
                    
                    if (menu_card != cards_.end()) {
                        // Select existing menu card
                        (*menu_card)->grab_focus = true;
                    } else {
                        // Create a new menu card if none exists
                        create_card("menu", true);
                    }
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_F)) {
                    // Handle Ctrl+Shift+F shortcut - Fit to width
                    float total_width = calculate_total_cards_width();
                    if (total_width > 0) {
                        // Use the resize_window service to adjust the main window width
                        auto resize_service = registrar::get<std::function<void(int, int)>>("resize_window");
                        if (resize_service) {
                            // Get current window height to maintain it
                            SDL_Window* window = nullptr;
                            auto get_window_service = registrar::get<std::function<SDL_Window*()>>("get_window");
                            if (get_window_service) {
                                window = (*get_window_service)();
                            }
                            
                            int current_height = 600; // default fallback
                            if (window) {
                                int current_width = 0;
                                SDL_GetWindowSize(window, &current_width, &current_height);
                                
                                // Check if window is maximized and skip if so
                                SDL_WindowFlags flags = SDL_GetWindowFlags(window);
                                if (flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN)) {
                                    // Don't resize if maximized or fullscreen
                                    return;
                                }
                            }
                            
                            // Add some padding to the total width for window decorations and margins
                            int target_width = static_cast<int>(total_width + 0.0f); // 2px padding
                            (*resize_service)(target_width, current_height);
                        }
                    }
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_S)) {
                    // Find the first card that is focused
                    auto focused_card = std::find_if(cards_.begin(), cards_.end(),
                        [](const auto& card) { return card->is_focused; });
                    if (focused_card != cards_.end()) {
                        // Get a snapshot of the focused card
                        // Create a unique filename with timestamp
                        auto now = std::chrono::system_clock::now();
                        auto now_time_t = std::chrono::system_clock::to_time_t(now);
                        std::stringstream ss;
                        ss << "card_" << std::put_time(std::localtime(&now_time_t), "%Y%m%d_%H%M%S") << ".png";
                        std::string filename = ss.str();
                        
                        // Create a render function that draws the card content
                        auto render_function = [this, &focused_card]() {
                            auto &c = *(*focused_card);
                            // Create a temporary rendering environment for the card
                            // without its window decorations
                            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

                            ImGui::PushStyleColor(ImGuiCol_WindowBg, background_color);
                            ImGui::PushStyleColor(ImGuiCol_TitleBg, background_color);
                            ImGui::PushStyleColor(ImGuiCol_Text, text_color);                
                            color_setup colors(c.get_color(0), c.get_color(1));
                            
                            ui_context_.prepare();
                            c.render(ui_context_);
                            ImGui::PopStyleColor(3); // Pop the style colors
                            ImGui::PopStyleVar(2); // Pop the style variables
                        };
                        
                        // Get card dimensions
                        int width = static_cast<int>((*focused_card)->width);
                        int height = static_cast<int>(450.0f); // Use the standard card height
                        
                        // Use our capture helper to get a texture with the card contents
                        RouenGPUTexture* snapshot_texture = rouen::helpers::capture_imgui(
                            width, height, render_function, renderer);
                        
                        if (snapshot_texture) {
                            // Create a surface to store the pixel data
                            SDL_Surface* surface = rouen::helpers::download_gpu_texture(
                                renderer, snapshot_texture, width, height);
                            
                            if (surface) {
                                // Save the surface as PNG
                                if (IMG_SavePNG(surface, filename.c_str())) {
                                    // Success - show a message or notification if desired
                                    std::cout << "Card snapshot saved to " << filename << '\n';
                                } else {
                                    std::cerr << "Failed to save snapshot: " << SDL_GetError() << '\n';
                                }
                                
                                // Clean up the surface
                                SDL_DestroySurface(surface);
                            } else {
                                std::cerr << "Failed to download GPU texture to surface\n";
                            }
                            
                            // Clean up the texture
                            TextureHelper::destroyTexture(snapshot_texture);
                        }
                    }
            }
            
                else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                    // Handle Ctrl+Right Arrow shortcut
                    auto focused_card = std::find_if(cards_.begin(), cards_.end(),
                        [](const auto& card) { return card->is_focused; });
                    if (focused_card != cards_.end()) {
                        // Move the focused card to the right
                        if (focused_card + 1 != cards_.end()) {
                            std::iter_swap(focused_card, focused_card + 1);
                        }
                    }
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
                    // Handle Ctrl+Left Arrow shortcut
                    auto focused_card = std::find_if(cards_.begin(), cards_.end(),
                        [](const auto& card) { return card->is_focused; });
                    if (focused_card != cards_.begin()) {
                        // Move the focused card to the left
                        std::iter_swap(focused_card, focused_card - 1);
                    }
                }
            }
        }
    }
    
    // Save current cards to ImGui configuration
    void save_card_uris() {
        // Create a string with all card URIs separated by semicolons
        std::string uris;
        for (const auto& card : cards_) {
            if (!uris.empty()) {
                uris += ";";
            }
            uris += card->get_uri();
        }
        
        // Save to rouen.ini in the user's config directory (always writable)
        std::filesystem::path rouen_ini_path = rouen::platform::get_user_config_directory() / "rouen.ini";
        
        // First read from user config path, fallback to bundle resource path if not present to preserve other options
        std::filesystem::path read_path = rouen_ini_path;
        if (!std::filesystem::exists(read_path)) {
            read_path = rouen::platform::get_resource_path("rouen.ini");
        }
        
        std::ifstream ini_file(read_path);
        std::stringstream buffer;
        bool found_rouen_section = false;
        bool in_rouen_section = false;

        if (ini_file) {
            std::string line;
            while (std::getline(ini_file, line)) {
                // Check for rouen section
                if (line == "[rouen]") {
                    found_rouen_section = true;
                    in_rouen_section = true;
                    buffer << line << '\n';
                    buffer << "cards=" << uris << '\n';
                    continue;
                }
                
                // Check for new section after rouen
                if (in_rouen_section && !line.empty() && line[0] == '[') {
                    in_rouen_section = false;
                }
                
                // Skip card entries in rouen section, keep everything else
                if (!in_rouen_section || line.substr(0, 6) != "cards=") {
                    buffer << line << '\n';
                }
            }
            ini_file.close();
        }
        
        // If rouen section wasn't found, add it
        if (!found_rouen_section) {
            buffer << "[rouen]" << '\n';
            buffer << "cards=" << uris << '\n';
        }
        
        // Write back to the user config directory file
        std::ofstream out_file(rouen_ini_path);
        if (out_file) {
            out_file << buffer.str();
            out_file.close();
        }
    }
    
    // Load cards from ImGui configuration
    void load_card_uris() {
        if (no_initial_cards) {
            create_card("menu", true);
            return;
        }
        // Read from user config path, fallback to bundle resource path if not present
        std::filesystem::path rouen_ini_path = rouen::platform::get_user_config_directory() / "rouen.ini";
        if (!std::filesystem::exists(rouen_ini_path)) {
            rouen_ini_path = rouen::platform::get_resource_path("rouen.ini");
        }
        
        std::ifstream ini_file(rouen_ini_path);
        if (!ini_file) {
            // If file doesn't exist, create the default menu card
            create_card("menu", true);
            return;
        }
        
        std::string line;
        bool in_rouen_section = false;
        std::string uris;
        
        while (std::getline(ini_file, line)) {
            // Check for rouen section
            if (line == "[rouen]") {
                in_rouen_section = true;
                continue;
            }
            
            // Check for new section
            if (in_rouen_section && !line.empty() && line[0] == '[') {
                in_rouen_section = false;
                continue;
            }
            
            // Look for cards entry in rouen section
            if (in_rouen_section && line.substr(0, 6) == "cards=") {
                uris = line.substr(6);
                break;
            }
        }
        
        ini_file.close();
        
        // If no saved state, create the default menu card
        if (uris.empty()) {
            create_card("menu", true);
            return;
        }
        
        // Parse the string and create cards
        size_t pos = 0;
        while (pos < uris.length()) {
            size_t end = uris.find(';', pos);
            if (end == std::string::npos) {
                end = uris.length();
            }
            
            std::string uri = uris.substr(pos, end - pos);
            if (!uri.empty()) {
                create_card(uri);
            }
            
            pos = end + 1;
        }
    }
    
    struct render_status {
        int requested_fps {1};
    };

    [[nodiscard]] render_status render() {
        cards_to_cleanup_.clear();
        TextureHelper::cleanupFrame();
        // Dynamic window title merging: when there's only 1 card, merge its name with the OS frame window
        auto get_window_service = registrar::get<std::function<SDL_Window*()>>("get_window");
        if (get_window_service) {
            SDL_Window* window = (*get_window_service)();
            if (window) {
                if (cards_.size() == 1 && cards_[0]) {
                    std::string title = cards_[0]->window_title;
                    size_t hash_pos = title.find("##");
                    if (hash_pos != std::string::npos) {
                        title = title.substr(0, hash_pos);
                    }
                    std::string new_title = std::format("Rouen - {}", title);
                    SDL_SetWindowTitle(window, new_title.c_str());
                } else {
                    SDL_SetWindowTitle(window, "Rouen");
                }
            }
        }

        render_status result;
        handle_shortcuts();
        auto const size {ImGui::GetMainViewport()->Size};
        ImGui::PushStyleColor(ImGuiCol_WindowBg, background_color);
        ImGui::PushStyleColor(ImGuiCol_TitleBg, background_color);
        ImGui::PushStyleColor(ImGuiCol_Text, text_color);
        bool const empty_editor = editor_.empty();
        float const section_width = std::max(size.x, 1.0f);
        float const wf = get_width_factor();
        float estimated_total_width = 0.0f;
        for (const auto& c : cards_) {
            estimated_total_width += c->width;
        }
        float const row_max_width = std::max(size.x * wf, std::ceil(estimated_total_width / section_width) * section_width);

        /*
         * SECTION & WIDTH MULTIPLIER LAYOUT ALGORITHM:
         * 
         * 1. Section Width & Row Multiplier:
         *    - section_width = size.x (OS window viewport width).
         *    - wf = get_width_factor() (1.0x, 2.0x, 3.0x, etc., configured via Display Card).
         *    - row_max_width = section_width * wf.
         * 
         * 2. Section Boundary Alignment:
         *    - Within each row, cards are organized across section boundaries:
         *      Section 0: [0, section_width]
         *      Section 1: [section_width, 2 * section_width]
         *      Section N: [N * section_width, std::min((N+1)*section_width, row_max_width)]
         * 
         * 3. Last Fitting Window Expansion:
         *    - As cards are placed sequentially, if a card exceeds the current section boundary
         *      sec_boundary = (current_sec_idx + 1) * section_width:
         *      a) The last card placed in that section (the "last fitting window") expands its
         *         override_width to reach sec_boundary - last_item.abs_x.
         *      b) current_x advances to sec_boundary (the start of the next section).
         *      c) The current card is then evaluated against the new section or new row.
         *    - This guarantees every viewport section is perfectly filled with zero straddling or clipping.
         */
        if (empty_editor) {
            struct card_layout_item {
                std::shared_ptr<card> card_ptr;
                float abs_x { 0.0f };
                float scaled_width { 0.0f };
                float override_width { -1.0f };
            };

            std::vector<std::vector<card_layout_item>> rows;
            float current_x = 0.0f;
            int current_sec_idx = 0;
            rows.emplace_back();

            for (size_t idx = 0; idx < cards_.size(); ) {
                auto& c = cards_[idx];
                float const card_w = c->width;
                float const sec_boundary = static_cast<float>(current_sec_idx + 1) * section_width;

                if (current_x + card_w <= sec_boundary + 0.01f) {
                    rows.back().push_back({
                        .card_ptr = c,
                        .abs_x = current_x,
                        .scaled_width = card_w,
                        .override_width = -1.0f
                    });
                    current_x += card_w;
                    
                    if (std::abs(current_x - sec_boundary) < 0.01f) {
                        current_x = sec_boundary;
                        current_sec_idx++;
                        if (current_x >= row_max_width - 0.01f) {
                            current_x = 0.0f;
                            current_sec_idx = 0;
                            if (idx + 1 < cards_.size()) {
                                rows.emplace_back();
                            }
                        }
                    }
                    idx++;
                }
                else {
                    // Card does not fit in current section
                    if (current_x > static_cast<float>(current_sec_idx) * section_width + 0.01f) {
                        // Expand the last fitting card in this section so it reaches sec_boundary
                        auto& last_item = rows.back().back();
                        last_item.override_width = sec_boundary - last_item.abs_x;
                        current_x = sec_boundary;
                        current_sec_idx++;
                        
                        if (current_x >= row_max_width - 0.01f) {
                            current_x = 0.0f;
                            current_sec_idx = 0;
                            if (idx < cards_.size()) {
                                rows.emplace_back();
                            }
                        }
                        // Retry current card in the new section or row
                    }
                    else {
                        // Current section is empty, card_w > section_width
                        if (current_x + card_w > row_max_width + 0.01f && current_x > 0.01f) {
                            current_x = 0.0f;
                            current_sec_idx = 0;
                            rows.emplace_back();
                            // Retry current card at row start
                        }
                        else {
                            rows.back().push_back({
                                .card_ptr = c,
                                .abs_x = current_x,
                                .scaled_width = card_w,
                                .override_width = -1.0f
                            });
                            current_x += card_w;
                            current_sec_idx = static_cast<int>(current_x / section_width);
                            if (current_x >= row_max_width - 0.01f) {
                                current_x = 0.0f;
                                current_sec_idx = 0;
                                if (idx + 1 < cards_.size()) {
                                    rows.emplace_back();
                                }
                            }
                            idx++;
                        }
                    }
                }
            }

            if (!rows.empty() && rows.back().empty()) {
                rows.pop_back();
            }

            // Expand the last item in each section or row if not already expanded
            for (auto& row : rows) {
                if (!row.empty()) {
                    auto& last_item = row.back();
                    if (last_item.override_width < 0.0f) {
                        int sec_idx = static_cast<int>(last_item.abs_x / section_width);
                        float sec_boundary = static_cast<float>(sec_idx + 1) * section_width;
                        if (sec_boundary > last_item.abs_x) {
                            last_item.override_width = std::max(sec_boundary - last_item.abs_x, last_item.scaled_width);
                        }
                    }
                }
            }

            // Calculate dynamic total deck width from all layout items across rows
            float total_deck_width = size.x;
            for (const auto& row : rows) {
                for (const auto& item : row) {
                    float item_end_x = item.abs_x + ((item.override_width > 0.0f) ? item.override_width : item.scaled_width);
                    total_deck_width = std::max(total_deck_width, item_end_x);
                }
            }
            float const max_scroll = std::max(0.0f, total_deck_width - size.x);

            // If any card has requested grab_focus this frame, reset manual scroll mode
            bool grab_focus_requested = false;
            for (const auto& row : rows) {
                for (const auto& item : row) {
                    if (item.card_ptr && item.card_ptr->grab_focus) {
                        grab_focus_requested = true;
                        break;
                    }
                }
                if (grab_focus_requested) break;
            }
            if (grab_focus_requested) {
                is_scrolling_manually = false;
            }

            // Handle trackpad and Shift+mouse wheel horizontal scrolling
            if (max_scroll > 0.0f) {
                auto& io = ImGui::GetIO();
                float wheel_h = io.MouseWheelH;
                if (io.KeyShift && wheel_h == 0.0f) {
                    wheel_h = io.MouseWheel;
                }

                if (wheel_h != 0.0f) {
                    is_scrolling_manually = true;
                    float const scroll_speed = 40.0f;
                    target_scroll_x -= wheel_h * scroll_speed;
                    target_scroll_x = std::clamp(target_scroll_x, 0.0f, max_scroll);
                } else if (is_scrolling_manually) {
                    // Snap to the nearest section boundary when the user stops scrolling
                    int target_section = static_cast<int>(std::round(target_scroll_x / section_width));
                    float section_target = static_cast<float>(target_section) * section_width;
                    target_scroll_x = std::clamp(section_target, 0.0f, max_scroll);

                    // Find the first card in the target section and focus it
                    card::ptr new_focus_card = nullptr;
                    for (const auto& row : rows) {
                        for (const auto& item : row) {
                            if (item.card_ptr) {
                                int card_sec_idx = static_cast<int>(item.abs_x / section_width);
                                if (card_sec_idx == target_section) {
                                    new_focus_card = item.card_ptr;
                                    break;
                                }
                            }
                        }
                        if (new_focus_card) break;
                    }

                    if (new_focus_card) {
                        for (auto& c : cards_) {
                            c->is_focused = false;
                            c->grab_focus = false;
                        }
                        new_focus_card->grab_focus = true;
                        last_focused_card_ = new_focus_card;
                    } else {
                        for (auto& c : cards_) {
                            c->is_focused = false;
                            c->grab_focus = false;
                        }
                        last_focused_card_.reset();
                    }

                    is_scrolling_manually = false;
                }
            }

            // Scan all cards to update target_scroll_x: priority to grab_focus card, then last_focused_card_, then is_focused card
            card::ptr focus_target_card = nullptr;
            float target_card_abs_x = 0.0f;

            if (!is_scrolling_manually) {
                for (const auto& row : rows) {
                    for (const auto& item : row) {
                        if (item.card_ptr && item.card_ptr->grab_focus) {
                            focus_target_card = item.card_ptr;
                            target_card_abs_x = item.abs_x;
                            break;
                        }
                    }
                    if (focus_target_card) break;
                }

                if (!focus_target_card) {
                    if (auto last_locked = last_focused_card_.lock()) {
                        for (const auto& row : rows) {
                            for (const auto& item : row) {
                                if (item.card_ptr && item.card_ptr == last_locked) {
                                    focus_target_card = item.card_ptr;
                                    target_card_abs_x = item.abs_x;
                                    break;
                                }
                            }
                            if (focus_target_card) break;
                        }
                    }
                }

                if (!focus_target_card) {
                    for (const auto& row : rows) {
                        for (const auto& item : row) {
                            if (item.card_ptr && item.card_ptr->is_focused) {
                                focus_target_card = item.card_ptr;
                                target_card_abs_x = item.abs_x;
                                break;
                            }
                        }
                        if (focus_target_card) break;
                    }
                }
            }

            if (focus_target_card && !is_scrolling_manually) {
                int section_idx = static_cast<int>(target_card_abs_x / section_width);
                float section_target = static_cast<float>(section_idx) * section_width;
                target_scroll_x = std::clamp(section_target, 0.0f, max_scroll);
            } else {
                target_scroll_x = std::clamp(target_scroll_x, 0.0f, max_scroll);
            }

            // Smoothly interpolate current_scroll_x towards target_scroll_x
            float scroll_diff = target_scroll_x - current_scroll_x;
            if (std::abs(scroll_diff) > 0.5f) {
                float step = scroll_diff * 0.35f;
                if (std::abs(step) < 2.0f) {
                    step = (scroll_diff > 0.0f) ? 2.0f : -2.0f;
                }
                if (std::abs(step) >= std::abs(scroll_diff)) {
                    current_scroll_x = target_scroll_x;
                } else {
                    current_scroll_x += step;
                }
                result.requested_fps = std::max(result.requested_fps, 60);
            } else {
                current_scroll_x = target_scroll_x;
            }

            float const scaled_min_height = card::min_card_height;
            float const row_height { std::max(size.y / static_cast<float>(std::max<size_t>(rows.size(), 1)), scaled_min_height) };
            float y = 0.0f;
            std::set<card::ptr> cards_to_remove;

            for (auto& row : rows) {
                for (auto& item : row) {
                    auto& c = item.card_ptr;
                    float card_abs_x = item.abs_x;

                    float draw_x = card_abs_x - current_scroll_x;
                    float const card_w = (item.override_width > 0.0f) ? item.override_width : c->width;
                    bool const in_view = (draw_x + card_w >= -20.0f && draw_x <= size.x + 20.0f && y + row_height >= -20.0f && y <= size.y + 20.0f);
                    c->is_visible_in_viewport = in_view || c->is_focused || c->grab_focus;
                    bool render_result = render(*c, draw_x, row_height, result.requested_fps, y, item.override_width);
                    if (c->is_focused) {
                        c->grab_focus = false;
                        last_focused_card_ = c;
                    }
                    if (!render_result) {
                        // Unregister MCP functions and trigger close hook immediately when card fails to render
                        try {
                            c->unregister_mcp_functions();
                            c->on_close();
                        } catch (...) {
                            // Intentionally ignored: card might already be corrupted during cleanup
                            static_cast<void>(0);
                        }
                        cards_to_remove.insert(c);
                    }
                }
                y += row_height;
            }

            // Defer cleanup to prevent use-after-free crashes during the current frame rendering
            for (const auto& c : cards_to_remove) {
                cards_to_cleanup_.push_back(c);
            }

            auto cards_to_remove_main = std::remove_if(cards_.begin(), cards_.end(),
                [&cards_to_remove] (auto &c) {
                    return cards_to_remove.find(c) != cards_to_remove.end();
                 });
            
            // Cards have already been unregistered above, just erase them
            cards_.erase(cards_to_remove_main, cards_.end());
        }
        else {
            auto right_corner_offset {row_max_width};
            float left_corner {0.0f};
            for (auto& c : cards_) {
                float const scaled_card_width = c->width;
                right_corner_offset -= scaled_card_width + 2.0f;
                if (c->is_focused) {
                    left_corner = right_corner_offset + scaled_card_width - row_max_width + 2.0f;
                    break;
                }
            }
            static float last_viewport_width = row_max_width;
            if (std::abs(last_viewport_width - row_max_width) > std::numeric_limits<float>::epsilon()) {
                start_x = 0.0f;
                last_viewport_width = row_max_width;
            }
            if (start_x > right_corner_offset || (start_x - row_max_width) > right_corner_offset) {
                start_x = right_corner_offset;
            }
            start_x = std::max(start_x, left_corner);
            auto x{start_x};
            auto y = (empty_editor ? 450.0f : 250.0f);
            std::set<card::ptr> cards_to_remove;
            for (auto const& c : cards_) {
                float override_width = -1.0f;
                if (!cards_.empty() && c == cards_.back()) {
                    float const scaled_card_width = c->width;
                    if (row_max_width - x > scaled_card_width) {
                        override_width = row_max_width - x;
                    }
                }
                bool draw_ok = render(*c, x, y, result.requested_fps, 0.0f, override_width);
                if (c->is_focused) {
                    last_focused_card_ = c;
                }
                if (!draw_ok) {
                    try {
                        c->unregister_mcp_functions();
                        c->on_close();
                    } catch (...) {
                        static_cast<void>(0);
                    }
                    cards_to_remove.insert(c);
                }
            }

            // Defer cleanup to prevent use-after-free crashes during the current frame rendering
            for (const auto& c : cards_to_remove) {
                cards_to_cleanup_.push_back(c);
            }

            auto cards_to_remove_main = std::remove_if(cards_.begin(), cards_.end(),
                [&cards_to_remove] (auto &c) {
                    return cards_to_remove.find(c) != cards_to_remove.end();
                 });
            
            cards_.erase(cards_to_remove_main, cards_.end());

            // Render the editor window
            ImGui::SetNextWindowPos({0.0f, 2.0f + y}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({size.x, size.y - y}, ImGuiCond_Always);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, editor_background_color);
            editor_.render();
            ImGui::PopStyleColor();
        }

        // Save card state when a card is added or removed
        static size_t last_card_count = 0;
        if (cards_.size() != last_card_count) {
            save_card_uris();
            last_card_count = cards_.size();
        }

        ImGui::PopStyleColor(3);
        return result;
    }

    // Calculate total width needed for all cards
    float calculate_total_cards_width() const {
        float const wf = get_width_factor();
        float total_width = 0.0f;
        for (const auto& card : cards_) {
            total_width += (card->width * wf) + 2.0f; // Add spacing between cards
        }
        // Remove the extra spacing from the last card
        if (!cards_.empty()) {
            total_width -= 2.0f;
        }
        return total_width;
    }

    // Public accessor for all cards (needed for registrar iteration)
    const std::vector<std::shared_ptr<card>>& get_cards() const { return cards_; }

    // Try to close a focused card, return true if a card was closed
    bool close_focused_card() {
        auto focused_card = std::find_if(cards_.begin(), cards_.end(),
            [](const auto& card) { return card->is_focused; });
        if (focused_card != cards_.end()) {
            cards_to_cleanup_.push_back(*focused_card);
            cards_.erase(focused_card);
            return true;
        }
        return false;
    }

    std::vector<std::shared_ptr<card>>& get_cards() { return cards_; }
    [[nodiscard]] float get_target_scroll_x() const { return target_scroll_x; }

private:
    friend class DeckScrollingTest;
    SDL_Renderer* renderer;
    std::vector<std::shared_ptr<card>> cards_;
    std::vector<std::shared_ptr<card>> cards_to_cleanup_;
    ImVec4 background_color;
    ImVec4 editor_background_color;
    ImVec4 text_color;
    editor editor_;
    float start_x {2.0f};
    float current_scroll_x {0.0f};
    float target_scroll_x {0.0f};
    bool is_scrolling_manually {false};
    rouen::ui::imgui_ui_context_impl ui_context_;
    std::weak_ptr<card> last_focused_card_;
public:
    static inline bool no_initial_cards{false};
};
