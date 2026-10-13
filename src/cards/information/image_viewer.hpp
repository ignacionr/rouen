#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/sdl_compat.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/texture_helper.hpp"
#include "../../helpers/texture_utils.hpp"
#include "../../registrar.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

inline bool is_supported_image_extension(std::string_view ext) {
    std::string lower_ext;
    lower_ext.reserve(ext.size());
    for (char c : ext) lower_ext += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    return lower_ext == ".png"  || lower_ext == ".jpg"  || lower_ext == ".jpeg" ||
           lower_ext == ".bmp"  || lower_ext == ".gif"  || lower_ext == ".webp" ||
           lower_ext == ".tif"  || lower_ext == ".tiff" || lower_ext == ".tga"  ||
           lower_ext == ".avif" || lower_ext == ".jxl"  || lower_ext == ".svg"  ||
           lower_ext == ".ico"  || lower_ext == ".cur"  || lower_ext == ".pnm"  ||
           lower_ext == ".pbm"  || lower_ext == ".pgm"  || lower_ext == ".ppm"  ||
           lower_ext == ".xpm"  || lower_ext == ".xcf"  || lower_ext == ".qoi"  ||
           lower_ext == ".lbm"  || lower_ext == ".pcx";
}

struct image_viewer : public card {
    image_viewer(std::string_view locator = "") {
        // Theme Colors
        colors[0] = ImVec4(0.55f, 0.35f, 0.85f, 1.0f); // Primary - Vibrant Purple
        colors[1] = ImVec4(0.70f, 0.50f, 0.95f, 0.7f); // Secondary
        get_color(2, ImVec4(0.14f, 0.14f, 0.18f, 1.0f)); // Canvas Dark Background

        if (!locator.empty()) {
            std::string path_str = ::helpers::StringHelper::url_decode(locator);
            load_image(path_str);
        } else {
            name("Image Viewer");
        }
    }

    ~image_viewer() override {
        close_image();
    }

    std::string get_uri() const override {
        return std::format("image:{}", path_.string());
    }

    bool matches_uri(std::string_view uri) const override {
        return uri == "image" || uri.starts_with("image:") ||
               uri == "img"   || uri.starts_with("img:");
    }

    void handle_uri(std::string_view uri) override {
        if (uri.starts_with("image:")) {
            load_image(::helpers::StringHelper::url_decode(uri.substr(6)));
        } else if (uri.starts_with("img:")) {
            load_image(::helpers::StringHelper::url_decode(uri.substr(4)));
        }
    }

    void set_renderer(SDL_Renderer* renderer) {
        renderer_ = renderer;
        if (!image_texture_ && !path_.empty() && is_valid_) {
            load_image(path_.string());
        }
    }

    void load_image(const std::string& filepath) {
        close_image();

        path_ = std::filesystem::path(filepath);
        name(path_.filename().empty() ? "Image Viewer" : path_.filename().string());

        if (filepath.empty()) {
            status_message_ = "No file specified.";
            is_valid_ = false;
            return;
        }

        if (!std::filesystem::exists(path_)) {
            status_message_ = std::format("File not found: {}", filepath);
            is_valid_ = false;
            return;
        }

        try {
            std::uintmax_t bytes = std::filesystem::file_size(path_);
            file_size_bytes_ = static_cast<size_t>(bytes);
            if (bytes < 1024) {
                file_size_str_ = std::format("{} B", bytes);
            } else if (bytes < 1024 * 1024) {
                file_size_str_ = std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
            } else {
                file_size_str_ = std::format("{:.2f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
            }
        } catch (...) {
            file_size_bytes_ = 0;
            file_size_str_ = "Unknown size";
        }

        if (!renderer_) {
            try {
                auto r = registrar::get<SDL_Renderer*>("main_renderer");
                if (r && *r) renderer_ = *r;
            } catch (...) {}
        }

        if (!renderer_) {
            status_message_ = "SDL Renderer not available.";
            is_valid_ = false;
            return;
        }

        image_texture_ = TextureHelper::loadTextureFromFile(
            renderer_,
            filepath.c_str(),
            image_width_,
            image_height_
        );

        if (!image_texture_) {
            status_message_ = std::format("Failed to load image via SDL_Image: {}", SDL_GetError());
            is_valid_ = false;
            return;
        }

        zoom_mode_ = ZoomMode::FitWindow;
        custom_zoom_ = 1.0f;
        is_valid_ = true;
        status_message_.clear();
    }

    void close_image() {
        if (image_texture_) {
            TextureHelper::destroyTexture(image_texture_);
            image_texture_ = nullptr;
        }
        image_width_ = 0;
        image_height_ = 0;
        file_size_bytes_ = 0;
        is_valid_ = false;
    }

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override {
        glz::json_t card;
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

        std::vector<glz::json_t> body;

        // 1. Header Container
        glz::json_t header;
        header["type"] = "Container";
        header["style"] = "emphasis";

        glz::json_t col_set;
        col_set["type"] = "ColumnSet";
        std::vector<glz::json_t> columns;

        // Column 1: Emoji icon
        glz::json_t icon_col;
        icon_col["type"] = "Column";
        icon_col["width"] = "auto";
        std::vector<glz::json_t> icon_items;
        glz::json_t icon_tb;
        icon_tb["type"] = "TextBlock";
        icon_tb["text"] = "🖼️";
        icon_tb["size"] = "Large";
        icon_items.push_back(std::move(icon_tb));
        icon_col["items"] = std::move(icon_items);
        columns.push_back(std::move(icon_col));

        // Column 2: Title & subtitle
        glz::json_t title_col;
        title_col["type"] = "Column";
        title_col["width"] = "stretch";
        std::vector<glz::json_t> title_items;
        glz::json_t title_tb;
        title_tb["type"] = "TextBlock";
        title_tb["text"] = "Image Viewer & Inspector";
        title_tb["weight"] = "Bolder";
        title_tb["size"] = "Large";
        title_items.push_back(std::move(title_tb));

        glz::json_t sub_tb;
        sub_tb["type"] = "TextBlock";
        sub_tb["text"] = path_.empty() ? "No image loaded" : path_.string();
        sub_tb["isSubtle"] = true;
        sub_tb["size"] = "Small";
        title_items.push_back(std::move(sub_tb));
        title_col["items"] = std::move(title_items);
        columns.push_back(std::move(title_col));

        col_set["columns"] = std::move(columns);
        header["items"] = std::vector<glz::json_t>{std::move(col_set)};
        body.push_back(std::move(header));

        // 2. Image element
        glz::json_t img_elem;
        img_elem["type"] = "Image";
        std::string img_url;
        if (!path_.empty()) {
            std::string p = path_.string();
            img_url = p.starts_with("file://") ? p : ("file://" + p);
        }
        img_elem["url"] = img_url;
        img_elem["size"] = "Auto";
        img_elem["horizontalAlignment"] = "Center";
        img_elem["altText"] = "Preview image";
        body.push_back(std::move(img_elem));

        // 3. FactSet
        glz::json_t fact_set;
        fact_set["type"] = "FactSet";
        std::vector<glz::json_t> facts;

        // Resolution
        std::string res_val;
        if (image_width_ > 0 && image_height_ > 0) {
            std::string tag;
            if (image_width_ == 3840 && image_height_ == 2160) tag = " (4K UHD)";
            else if (image_width_ == 2560 && image_height_ == 1440) tag = " (1440p QHD)";
            else if (image_width_ == 1920 && image_height_ == 1080) tag = " (Full HD)";
            else if (image_width_ == 1280 && image_height_ == 720) tag = " (720p HD)";
            else if (image_width_ == 7680 && image_height_ == 4320) tag = " (8K UHD)";
            res_val = std::format("{} x {} pixels{}", image_width_, image_height_, tag);
        } else {
            res_val = "0 x 0 pixels (None)";
        }
        facts.push_back(glz::json_t::object_t{{"title", "Resolution:"}, {"value", res_val}});

        // Aspect Ratio
        std::string aspect_val = "N/A";
        if (image_width_ > 0 && image_height_ > 0) {
            int g = std::gcd(image_width_, image_height_);
            int aw = image_width_ / g;
            int ah = image_height_ / g;
            if (aw <= 32 && ah <= 32) {
                aspect_val = std::format("{}:{}", aw, ah);
            } else {
                aspect_val = std::format("{:.2f}:1", static_cast<double>(image_width_) / static_cast<double>(image_height_));
            }
        }
        facts.push_back(glz::json_t::object_t{{"title", "Aspect Ratio:"}, {"value", aspect_val}});

        // File Size
        std::string file_size_val;
        std::string ext = path_.extension().string();
        std::string fmt_name;
        if (!ext.empty()) {
            std::string lower_ext;
            for (char c : ext) lower_ext += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower_ext == ".png") fmt_name = "PNG";
            else if (lower_ext == ".jpg" || lower_ext == ".jpeg") fmt_name = "JPEG";
            else if (lower_ext == ".webp") fmt_name = "WebP";
            else if (lower_ext == ".bmp") fmt_name = "BMP";
            else if (lower_ext == ".gif") fmt_name = "GIF";
            else if (lower_ext == ".svg") fmt_name = "SVG";
            else if (lower_ext.size() > 1) fmt_name = lower_ext.substr(1);
        }
        std::string size_str = file_size_str_.empty() ? "0 B" : file_size_str_;
        if (!fmt_name.empty()) {
            file_size_val = std::format("{} ({} format)", size_str, fmt_name);
        } else {
            file_size_val = size_str;
        }
        facts.push_back(glz::json_t::object_t{{"title", "File Size:"}, {"value", file_size_val}});

        // Color Depth
        std::string depth_val = (image_width_ > 0 && image_height_ > 0) ? "32-bit RGBA" : "Unknown";
        facts.push_back(glz::json_t::object_t{{"title", "Color Depth:"}, {"value", depth_val}});

        // Zoom Level
        std::string zoom_str;
        if (zoom_mode_ == ZoomMode::FitWindow) zoom_str = "Fit Window";
        else if (zoom_mode_ == ZoomMode::Original) zoom_str = "100% (1:1)";
        else zoom_str = std::format("{:.0f}%", static_cast<double>(custom_zoom_ * 100.0f));
        facts.push_back(glz::json_t::object_t{{"title", "Zoom Level:"}, {"value", zoom_str}});

        // Rotation
        std::string rot_str = std::format("{}°", rotation_deg_);
        facts.push_back(glz::json_t::object_t{{"title", "Rotation:"}, {"value", rot_str}});

        fact_set["facts"] = std::move(facts);
        body.push_back(std::move(fact_set));

        // 4. Controls / Inputs Container
        glz::json_t controls_container;
        controls_container["type"] = "Container";
        std::vector<glz::json_t> control_items;

        glz::json_t zoom_input;
        zoom_input["type"] = "Input.Number";
        zoom_input["id"] = "zoom_factor";
        zoom_input["title"] = "Zoom Factor";
        zoom_input["value"] = std::format("{:.1f}", custom_zoom_);
        zoom_input["min"] = 0.1;
        zoom_input["max"] = 10.0;
        control_items.push_back(std::move(zoom_input));

        glz::json_t angle_input;
        angle_input["type"] = "Input.ChoiceSet";
        angle_input["id"] = "angle_deg";
        angle_input["title"] = "Rotate Angle";
        angle_input["value"] = std::format("{}", rotation_deg_ == 0 ? 90 : rotation_deg_);
        std::vector<glz::json_t> angle_choices;
        angle_choices.push_back(glz::json_t::object_t{{"title", "90°"}, {"value", "90"}});
        angle_choices.push_back(glz::json_t::object_t{{"title", "180°"}, {"value", "180"}});
        angle_choices.push_back(glz::json_t::object_t{{"title", "270°"}, {"value", "270"}});
        angle_input["choices"] = std::move(angle_choices);
        control_items.push_back(std::move(angle_input));

        controls_container["items"] = std::move(control_items);
        body.push_back(std::move(controls_container));

        card["body"] = std::move(body);

        // Actions
        std::vector<glz::json_t> actions;

        glz::json_t act_zoom;
        act_zoom["type"] = "Action.Execute";
        act_zoom["title"] = "Zoom In (+)";
        act_zoom["verb"] = "image_zoom";
        actions.push_back(std::move(act_zoom));

        glz::json_t act_fit;
        act_fit["type"] = "Action.Execute";
        act_fit["title"] = "Fit Window";
        act_fit["verb"] = "image_fit";
        actions.push_back(std::move(act_fit));

        glz::json_t act_rotate;
        act_rotate["type"] = "Action.Execute";
        act_rotate["title"] = "Rotate 90°";
        act_rotate["verb"] = "image_rotate";
        actions.push_back(std::move(act_rotate));

        card["actions"] = std::move(actions);

        std::string out;
        static_cast<void>(glz::write_json(card, out));
        return out;
    }

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override {
        if (action_json.empty()) return;

        try {
            std::string verb;
            glz::json_t data{};
            glz::json_t payload{};

            auto err = glz::read_json(payload, std::string(action_json));
            if (!err && payload.is_object()) {
                if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                    verb = payload["verb"].get<std::string>();
                } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                    verb = payload["action"].get<std::string>();
                }

                if (payload.contains("data") && payload["data"].is_object()) {
                    data = payload["data"];
                } else {
                    data = payload;
                }
            } else {
                verb = std::string(action_json);
            }

            if (verb == "image_zoom") {
                last_action_ = "image_zoom";
                bool factor_found = false;
                double factor_val = 1.0;

                auto extract_factor = [&](const glz::json_t& obj) {
                    if (!obj.is_object()) return;
                    if (obj.contains("zoom_factor")) {
                        if (obj["zoom_factor"].holds<double>()) {
                            factor_val = obj["zoom_factor"].get<double>();
                            factor_found = true;
                        } else if (obj["zoom_factor"].holds<std::string>()) {
                            try {
                                factor_val = std::stod(obj["zoom_factor"].get<std::string>());
                                factor_found = true;
                            } catch (...) {}
                        }
                    } else if (obj.contains("factor")) {
                        if (obj["factor"].holds<double>()) {
                            factor_val = obj["factor"].get<double>();
                            factor_found = true;
                        } else if (obj["factor"].holds<std::string>()) {
                            try {
                                factor_val = std::stod(obj["factor"].get<std::string>());
                                factor_found = true;
                            } catch (...) {}
                        }
                    } else if (obj.contains("zoom")) {
                        if (obj["zoom"].holds<double>()) {
                            factor_val = obj["zoom"].get<double>();
                            factor_found = true;
                        } else if (obj["zoom"].holds<std::string>()) {
                            try {
                                factor_val = std::stod(obj["zoom"].get<std::string>());
                                factor_found = true;
                            } catch (...) {}
                        }
                    }
                };

                extract_factor(data);
                if (!factor_found && payload.is_object()) {
                    extract_factor(payload);
                }

                if (factor_found) {
                    custom_zoom_ = std::clamp(static_cast<float>(factor_val), 0.1f, 10.0f);
                    zoom_mode_ = ZoomMode::Custom;
                } else {
                    if (zoom_mode_ != ZoomMode::Custom) {
                        custom_zoom_ = 1.0f;
                    }
                    zoom_mode_ = ZoomMode::Custom;
                    custom_zoom_ = std::min(custom_zoom_ + 0.2f, 10.0f);
                }
            } else if (verb == "image_rotate") {
                last_action_ = "image_rotate";
                bool angle_found = false;
                int angle_val = 0;
                bool is_relative = false;

                auto extract_angle = [&](const glz::json_t& obj) {
                    if (!obj.is_object()) return;
                    auto check_key = [&](std::string_view k) {
                        if (!obj.contains(k)) return;
                        if (obj[k].holds<double>()) {
                            angle_val = static_cast<int>(obj[k].get<double>());
                            angle_found = true;
                        } else if (obj[k].holds<std::string>()) {
                            std::string s = obj[k].get<std::string>();
                            if (s.starts_with('+') || s.starts_with('-')) is_relative = true;
                            try {
                                angle_val = std::stoi(s);
                                angle_found = true;
                            } catch (...) {}
                        }
                    };
                    check_key("angle_deg");
                    if (!angle_found) check_key("angle");
                    if (!angle_found) check_key("rotation_deg");
                    if (!angle_found) check_key("rotation");
                };

                extract_angle(data);
                if (!angle_found && payload.is_object()) {
                    extract_angle(payload);
                }

                if (angle_found) {
                    if (is_relative) {
                        rotation_deg_ = (rotation_deg_ + angle_val) % 360;
                    } else {
                        rotation_deg_ = (angle_val % 360 + 360) % 360;
                    }
                    if (rotation_deg_ < 0) rotation_deg_ += 360;
                } else {
                    // Default step when clicking button "Rotate 90°"
                    rotation_deg_ = (rotation_deg_ + 90) % 360;
                }
            } else if (verb == "image_fit") {
                last_action_ = "image_fit";
                zoom_mode_ = ZoomMode::FitWindow;
                custom_zoom_ = 1.0f;
            } else if (verb == "toggle_adaptive_view") {
                last_action_ = "toggle_adaptive_view";
                adaptive_view_mode = !adaptive_view_mode;
            }

            last_adaptive_parse_time_ = {};
        } catch (const std::exception& e) {
            std::cerr << "[image] Action error: " << e.what() << std::endl;
        }
    }

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }
    [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }
    [[nodiscard]] float get_zoom_level() const noexcept { return custom_zoom_; }
    [[nodiscard]] float get_zoom_factor() const noexcept { return custom_zoom_; }
    [[nodiscard]] int get_rotation_deg() const noexcept { return rotation_deg_; }
    [[nodiscard]] int get_rotation_degrees() const noexcept { return rotation_deg_; }
    [[nodiscard]] size_t get_file_size_bytes() const noexcept { return file_size_bytes_; }
    [[nodiscard]] std::string get_image_path() const { return path_.string(); }
    [[nodiscard]] int get_image_width() const noexcept { return image_width_; }
    [[nodiscard]] int get_image_height() const noexcept { return image_height_; }
    [[nodiscard]] bool is_fit_to_window() const noexcept { return zoom_mode_ == ZoomMode::FitWindow; }

    bool render() override {
        rouen::ui::imgui_ui_context_impl ui;
        return render(ui);
    }

    bool render(rouen::ui::ui_context& ui) override {
        return render_window([this, &ui]() {
            // Presentation mode selector
            ui.checkbox("Adaptive Card View", &adaptive_view_mode);
            ui.separator();

            if (adaptive_view_mode) {
                auto now = std::chrono::steady_clock::now();
                auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_parse_time_).count();
                if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                    try {
                        adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                        adaptive_error_.clear();
                    } catch (const std::exception& e) {
                        adaptive_error_ = e.what();
                    }
                    last_adaptive_parse_time_ = now;
                }

                if (!adaptive_error_.empty()) {
                    ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
                } else {
                    adaptive_renderer_.render(
                        adaptive_bound_, adaptive_input_state_,
                        helpers::adaptive_cards::renderer::action_callbacks{
                            .open_url = [](const std::string& url) {
                                static_cast<void>(rouen::platform::open_url(url));
                            },
                            .on_submit = [this](const std::string& payload) {
                                handle_action(payload);
                            }
                        },
                        helpers::adaptive_cards::render_config{
                            .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                            .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                            .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                        },
                        [this](const std::string& /*url*/, int& w, int& h) -> RouenGPUTexture* {
                            w = image_width_;
                            h = image_height_;
                            return image_texture_;
                        }
                    );
                }
                return;
            }

            render_toolbar();

            if (!is_valid_) {
                ImGui::Separator();
                ImGui::Spacing();
                if (!status_message_.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", status_message_.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No image file opened.");
                }
                ImGui::Spacing();

                static char path_buf[512] = "";
                ImGui::Text("Open Image File Path:");
                ImGui::InputText("##img_path", path_buf, sizeof(path_buf));
                ImGui::SameLine();
                if (ImGui::Button("Open Image")) {
                    load_image(path_buf);
                }
                return;
            }

            // Keyboard navigation / shortcuts
            if (ImGui::IsWindowFocused()) {
                if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
                    zoom_mode_ = ZoomMode::Custom;
                    custom_zoom_ = std::min(custom_zoom_ + 0.2f, 10.0f);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
                    zoom_mode_ = ZoomMode::Custom;
                    custom_zoom_ = std::max(custom_zoom_ - 0.2f, 0.1f);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_0)) {
                    zoom_mode_ = ZoomMode::FitWindow;
                }
            }

            // Canvas Child Window
            if (ImGui::BeginChild("ImageCanvas", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar)) {
                if (image_texture_) {
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    float orig_w = static_cast<float>(image_width_);
                    float orig_h = static_cast<float>(image_height_);
                    if (rotation_deg_ == 90 || rotation_deg_ == 270) {
                        std::swap(orig_w, orig_h);
                    }
                    float aspect = (orig_h > 0.0f) ? (orig_w / orig_h) : 1.0f;

                    float draw_w = orig_w;
                    float draw_h = orig_h;

                    if (zoom_mode_ == ZoomMode::FitWindow) {
                        if (avail.x / aspect <= avail.y) {
                            draw_w = avail.x;
                            draw_h = avail.x / aspect;
                        } else {
                            draw_h = avail.y;
                            draw_w = avail.y * aspect;
                        }
                    } else if (zoom_mode_ == ZoomMode::Original) {
                        draw_w = orig_w;
                        draw_h = orig_h;
                    } else { // Custom
                        draw_w = orig_w * custom_zoom_;
                        draw_h = orig_h * custom_zoom_;
                    }

                    if (draw_w < 1.0f) draw_w = 1.0f;
                    if (draw_h < 1.0f) draw_h = 1.0f;

                    // Center inside child container if smaller than container
                    if (draw_w < avail.x) {
                        ImGui::SetCursorPosX((avail.x - draw_w) * 0.5f);
                    }
                    if (draw_h < avail.y) {
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (avail.y - draw_h) * 0.5f);
                    }

                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    if (rotation_deg_ == 90) {
                        ImGui::GetWindowDrawList()->AddImageQuad(
                            rouen::helpers::texture_id_cast(image_texture_),
                            pos, ImVec2(pos.x + draw_w, pos.y),
                            ImVec2(pos.x + draw_w, pos.y + draw_h), ImVec2(pos.x, pos.y + draw_h),
                            ImVec2(0, 1), ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1)
                        );
                        ImGui::Dummy(ImVec2(draw_w, draw_h));
                    } else if (rotation_deg_ == 270) {
                        ImGui::GetWindowDrawList()->AddImageQuad(
                            rouen::helpers::texture_id_cast(image_texture_),
                            pos, ImVec2(pos.x + draw_w, pos.y),
                            ImVec2(pos.x + draw_w, pos.y + draw_h), ImVec2(pos.x, pos.y + draw_h),
                            ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), ImVec2(0, 0)
                        );
                        ImGui::Dummy(ImVec2(draw_w, draw_h));
                    } else if (rotation_deg_ == 180) {
                        ImGui::Image(rouen::helpers::texture_id_cast(image_texture_), ImVec2(draw_w, draw_h), ImVec2(1, 1), ImVec2(0, 0));
                    } else {
                        ImGui::Image(rouen::helpers::texture_id_cast(image_texture_), ImVec2(draw_w, draw_h));
                    }
                }
            }
            ImGui::EndChild();
        });
    }

private:
    enum class ZoomMode { FitWindow, Original, Custom };

    void render_toolbar() {
        ImGui::PushStyleColor(ImGuiCol_Header, colors[0]);

        if (is_valid_) {
            ImGui::Text("%dx%d", image_width_, image_height_);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(%s)", file_size_str_.c_str());
            ImGui::SameLine();
            ImGui::Text("|");
            ImGui::SameLine();

            if (ImGui::Button("Fit Window")) {
                zoom_mode_ = ZoomMode::FitWindow;
            }
            ImGui::SameLine();

            if (ImGui::Button("100% (1:1)")) {
                zoom_mode_ = ZoomMode::Original;
            }
            ImGui::SameLine();

            if (ImGui::Button("-")) {
                if (zoom_mode_ != ZoomMode::Custom) {
                    custom_zoom_ = 1.0f;
                }
                zoom_mode_ = ZoomMode::Custom;
                custom_zoom_ = std::max(custom_zoom_ - 0.2f, 0.1f);
            }
            ImGui::SameLine();

            if (zoom_mode_ == ZoomMode::FitWindow) {
                ImGui::Text("Fit");
            } else if (zoom_mode_ == ZoomMode::Original) {
                ImGui::Text("100%%");
            } else {
                ImGui::Text("%.0f%%", static_cast<double>(custom_zoom_ * 100.0f));
            }
            ImGui::SameLine();

            if (ImGui::Button("+")) {
                if (zoom_mode_ != ZoomMode::Custom) {
                    custom_zoom_ = 1.0f;
                }
                zoom_mode_ = ZoomMode::Custom;
                custom_zoom_ = std::min(custom_zoom_ + 0.2f, 10.0f);
            }
            ImGui::SameLine();

            if (ImGui::Button("Rotate 90°")) {
                rotation_deg_ = (rotation_deg_ + 90) % 360;
            }
        }

        ImGui::PopStyleColor();
    }

    std::filesystem::path path_;
    SDL_Renderer* renderer_{nullptr};
    RouenGPUTexture* image_texture_{nullptr};
    int image_width_{0};
    int image_height_{0};
    size_t file_size_bytes_{0};
    std::string file_size_str_;

    ZoomMode zoom_mode_{ZoomMode::FitWindow};
    float custom_zoom_{1.0f};
    int rotation_deg_{0};
    std::string last_action_{};

    std::string status_message_;
    bool is_valid_{false};

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};
};

} // namespace rouen::cards
