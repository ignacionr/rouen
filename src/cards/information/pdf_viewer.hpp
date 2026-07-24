#pragma once

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <fpdfview.h>
#include <fpdf_doc.h>

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/sdl_compat.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/texture_helper.hpp"
#include "../../helpers/texture_utils.hpp"
#include "../../registrar.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

inline void init_pdfium_library() {
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        FPDF_LIBRARY_CONFIG config;
        std::memset(&config, 0, sizeof(config));
        config.version = 2;
        config.m_pUserFontPaths = nullptr;
        config.m_pIsolate = nullptr;
        config.m_v8EmbedderSlot = 0;
        FPDF_InitLibraryWithConfig(&config);
    });
}

struct pdf_viewer : public card {
    pdf_viewer(std::string_view locator = "") {
        init_pdfium_library();
        
        width = 600.0f; // Double default card width for optimal PDF page display

        // Colors
        colors[0] = ImVec4(0.85f, 0.25f, 0.25f, 1.0f); // Primary - PDF Red
        colors[1] = ImVec4(0.95f, 0.45f, 0.45f, 0.7f); // Secondary
        get_color(2, ImVec4(0.18f, 0.18f, 0.22f, 1.0f)); // Background
        get_color(3, ImVec4(0.25f, 0.25f, 0.30f, 1.0f)); // Controls background

        if (!locator.empty()) {
            std::string path_str = ::helpers::StringHelper::url_decode(locator);
            load_pdf(path_str);
        } else {
            name("PDF Viewer");
        }
    }

    ~pdf_viewer() override {
        close_pdf();
        if (page_texture_) {
            TextureHelper::destroyTexture(page_texture_);
            page_texture_ = nullptr;
        }
    }

    std::string get_uri() const override {
        return std::format("pdf:{}", path_.string());
    }

    bool matches_uri(std::string_view uri) const override {
        return uri == "pdf" || uri.starts_with("pdf:");
    }

    void handle_uri(std::string_view uri) override {
        if (uri.starts_with("pdf:")) {
            std::string path_str = ::helpers::StringHelper::url_decode(uri.substr(4));
            load_pdf(path_str);
        }
    }

    void set_renderer(SDL_Renderer* renderer) {
        renderer_ = renderer;
        if (is_valid_ && doc_ && !page_texture_) {
            texture_needs_update_ = true;
        }
    }

    void load_pdf(const std::string& filepath) {
        close_pdf();
        
        path_ = std::filesystem::path(filepath);
        name(path_.filename().empty() ? "PDF Viewer" : path_.filename().string());

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

        doc_ = FPDF_LoadDocument(filepath.c_str(), nullptr);
        if (!doc_) {
            unsigned long err = FPDF_GetLastError();
            if (err == FPDF_ERR_PASSWORD) {
                status_message_ = "Document is password protected.";
            } else {
                status_message_ = std::format("Failed to open PDF document (Error code: {}).", err);
            }
            is_valid_ = false;
            return;
        }

        page_count_ = FPDF_GetPageCount(doc_);
        current_page_ = 0;
        zoom_ = 1.0f;
        rotation_ = 0;
        is_valid_ = true;
        status_message_.clear();
        texture_needs_update_ = true;
    }

    void close_pdf() {
        if (doc_) {
            FPDF_CloseDocument(doc_);
            doc_ = nullptr;
        }
        page_count_ = 0;
        current_page_ = 0;
        is_valid_ = false;
        if (page_texture_) {
            TextureHelper::destroyTexture(page_texture_);
            page_texture_ = nullptr;
        }
    }

    void render_page_texture() {
        if (!doc_ || page_count_ <= 0 || current_page_ < 0 || current_page_ >= page_count_) {
            return;
        }

        if (!renderer_) {
            try {
                auto r = registrar::get<SDL_Renderer*>("main_renderer");
                if (r && *r) renderer_ = *r;
            } catch (...) {}
        }

        if (!renderer_) {
            status_message_ = "SDL Renderer not available.";
            return;
        }

        FPDF_PAGE page = FPDF_LoadPage(doc_, current_page_);
        if (!page) {
            status_message_ = std::format("Failed to load page {}.", current_page_ + 1);
            return;
        }

        float orig_w = FPDF_GetPageWidthF(page);
        float orig_h = FPDF_GetPageHeightF(page);

        if (orig_w <= 0.0f || orig_h <= 0.0f) {
            FPDF_ClosePage(page);
            status_message_ = "Invalid page dimensions.";
            return;
        }

        // Base render DPI scale factor (1.5x scale for sharp text rendering)
        float base_scale = 1.5f * zoom_;
        int render_w = static_cast<int>(orig_w * base_scale);
        int render_h = static_cast<int>(orig_h * base_scale);

        if (render_w <= 0) render_w = 1;
        if (render_h <= 0) render_h = 1;

        SDL_Surface* surface = SDL_CreateSurface(render_w, render_h, SDL_PIXELFORMAT_RGBA32);
        if (surface) {
            FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(render_w, render_h, FPDFBitmap_BGRA, surface->pixels, surface->pitch);
            if (bitmap) {
                // Fill background white
                FPDFBitmap_FillRect(bitmap, 0, 0, render_w, render_h, 0xFFFFFFFF);
                
                // FPDF_REVERSE_BYTE_ORDER outputs RGBA for SDL_PIXELFORMAT_RGBA32
                int flags = FPDF_ANNOT | FPDF_LCD_TEXT | FPDF_REVERSE_BYTE_ORDER;
                FPDF_RenderPageBitmap(bitmap, page, 0, 0, render_w, render_h, rotation_, flags);
                FPDFBitmap_Destroy(bitmap);
            }

            if (page_texture_) {
                TextureHelper::destroyTexture(page_texture_);
                page_texture_ = nullptr;
            }

            page_texture_ = TextureHelper::createTextureFromSurface(renderer_, surface);
            tex_width_ = render_w;
            tex_height_ = render_h;
            SDL_DestroySurface(surface);
        }

        FPDF_ClosePage(page);
        texture_needs_update_ = false;
    }

    bool render() override {
        return render_window([this]() {
            render_toolbar();
            
            if (texture_needs_update_) {
                render_page_texture();
            }

            if (!is_valid_) {
                ImGui::Separator();
                ImGui::Spacing();
                if (!status_message_.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", status_message_.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No PDF file currently opened.");
                }
                ImGui::Spacing();
                
                static char path_buf[512] = "";
                ImGui::Text("Open File Path:");
                ImGui::InputText("##pdf_path", path_buf, sizeof(path_buf));
                ImGui::SameLine();
                if (ImGui::Button("Open PDF")) {
                    load_pdf(path_buf);
                }
                return;
            }

            // Keyboard navigation when window is focused
            if (ImGui::IsWindowFocused()) {
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
                    if (current_page_ > 0) {
                        current_page_--;
                        texture_needs_update_ = true;
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
                    if (current_page_ < page_count_ - 1) {
                        current_page_++;
                        texture_needs_update_ = true;
                    }
                }
            }

            // Main PDF View Canvas
            if (ImGui::BeginChild("PDFCanvas", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar)) {
                if (page_texture_) {
                    float avail_w = ImGui::GetContentRegionAvail().x;
                    float draw_w = static_cast<float>(tex_width_) / 1.5f;
                    float draw_h = static_cast<float>(tex_height_) / 1.5f;

                    // Center page if smaller than window width
                    if (draw_w < avail_w) {
                        ImGui::SetCursorPosX((avail_w - draw_w) * 0.5f);
                    }

                    ImGui::Image(rouen::helpers::texture_id_cast(page_texture_), ImVec2(draw_w, draw_h));
                }
            }
            ImGui::EndChild();
        });
    }

private:
    void render_toolbar() {
        ImGui::PushStyleColor(ImGuiCol_Header, colors[0]);

        if (is_valid_) {
            // Navigation controls
            if (ImGui::Button("◀ Prev") || (ImGui::IsKeyPressed(ImGuiKey_PageUp) && ImGui::IsWindowFocused())) {
                if (current_page_ > 0) {
                    current_page_--;
                    texture_needs_update_ = true;
                }
            }
            ImGui::SameLine();
            
            ImGui::Text("Page %d of %d", current_page_ + 1, page_count_);
            ImGui::SameLine();

            if (ImGui::Button("Next ▶") || (ImGui::IsKeyPressed(ImGuiKey_PageDown) && ImGui::IsWindowFocused())) {
                if (current_page_ < page_count_ - 1) {
                    current_page_++;
                    texture_needs_update_ = true;
                }
            }
            
            ImGui::SameLine();
            ImGui::Text("|");
            ImGui::SameLine();

            // Zoom controls
            if (ImGui::Button("-")) {
                if (zoom_ > 0.3f) {
                    zoom_ -= 0.15f;
                    texture_needs_update_ = true;
                }
            }
            ImGui::SameLine();
            ImGui::Text("%.0f%%", static_cast<double>(zoom_ * 100.0f));
            ImGui::SameLine();
            if (ImGui::Button("+")) {
                if (zoom_ < 4.0f) {
                    zoom_ += 0.15f;
                    texture_needs_update_ = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                zoom_ = 1.0f;
                texture_needs_update_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("↻ Rotate")) {
                rotation_ = (rotation_ + 1) % 4;
                texture_needs_update_ = true;
            }
        }

        ImGui::PopStyleColor();
    }

    std::filesystem::path path_;
    FPDF_DOCUMENT doc_{nullptr};
    int page_count_{0};
    int current_page_{0};
    float zoom_{1.0f};
    int rotation_{0};

    SDL_Renderer* renderer_{nullptr};
    RouenGPUTexture* page_texture_{nullptr};
    int tex_width_{0};
    int tex_height_{0};
    bool texture_needs_update_{false};

    std::string status_message_;
    bool is_valid_{false};
};

} // namespace rouen::cards
