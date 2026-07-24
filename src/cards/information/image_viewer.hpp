#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../../helpers/imgui_include.hpp"
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
            if (bytes < 1024) {
                file_size_str_ = std::format("{} B", bytes);
            } else if (bytes < 1024 * 1024) {
                file_size_str_ = std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
            } else {
                file_size_str_ = std::format("{:.2f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
            }
        } catch (...) {
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
        is_valid_ = false;
    }

    bool render() override {
        return render_window([this]() {
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

                    ImGui::Image(rouen::helpers::texture_id_cast(image_texture_), ImVec2(draw_w, draw_h));
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
        }

        ImGui::PopStyleColor();
    }

    std::filesystem::path path_;
    SDL_Renderer* renderer_{nullptr};
    RouenGPUTexture* image_texture_{nullptr};
    int image_width_{0};
    int image_height_{0};
    std::string file_size_str_;

    ZoomMode zoom_mode_{ZoomMode::FitWindow};
    float custom_zoom_{1.0f};

    std::string status_message_;
    bool is_valid_{false};
};

} // namespace rouen::cards
