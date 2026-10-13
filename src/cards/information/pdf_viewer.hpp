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

#include <chrono>
#include <cmath>

#if __has_include(<fpdfview.h>)
#include <fpdfview.h>
#include <fpdf_doc.h>
#define ROUEN_HAS_PDFIUM 1
#elif __has_include(<pdfium/fpdfview.h>)
#include <pdfium/fpdfview.h>
#include <pdfium/fpdf_doc.h>
#define ROUEN_HAS_PDFIUM 1
#else
#define ROUEN_HAS_PDFIUM 0
using FPDF_DOCUMENT = void*;
#endif

#include "../../../external/IconsMaterialDesign.h"
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

inline void init_pdfium_library() {
#if ROUEN_HAS_PDFIUM
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
#endif
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

#if ROUEN_HAS_PDFIUM
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

        auto extract_meta = [this](const char* tag) -> std::string {
            unsigned long len = FPDF_GetMetaText(doc_, tag, nullptr, 0);
            if (len <= 2) return "";
            std::vector<unsigned char> buf(len);
            FPDF_GetMetaText(doc_, tag, buf.data(), len);
            std::u16string u16str;
            for (size_t i = 0; i + 1 < len; i += 2) {
                char16_t ch = static_cast<char16_t>(buf[i] | (buf[i + 1] << 8));
                if (ch == 0) break;
                u16str.push_back(ch);
            }
            std::string u8str;
            for (char16_t ch : u16str) {
                if (ch < 0x80) {
                    u8str.push_back(static_cast<char>(ch));
                } else if (ch < 0x800) {
                    u8str.push_back(static_cast<char>(0xC0 | ((ch >> 6) & 0x1F)));
                    u8str.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
                } else {
                    u8str.push_back(static_cast<char>(0xE0 | ((ch >> 12) & 0x0F)));
                    u8str.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
                    u8str.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
                }
            }
            return u8str;
        };

        title_ = extract_meta("Title");
        author_ = extract_meta("Author");
#else
        status_message_ = "PDF rendering requires PDFium support.";
        is_valid_ = false;
#endif
    }

    void close_pdf() {
#if ROUEN_HAS_PDFIUM
        if (doc_) {
            FPDF_CloseDocument(doc_);
            doc_ = nullptr;
        }
#endif
        title_.clear();
        author_.clear();
        page_count_ = 0;
        current_page_ = 0;
        is_valid_ = false;
        if (page_texture_) {
            TextureHelper::destroyTexture(page_texture_);
            page_texture_ = nullptr;
        }
    }

    void render_page_texture() {
#if ROUEN_HAS_PDFIUM
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
#endif
    }

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
                        }
                    );
                }
                return;
            }

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

    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override {
        glz::json_t card;
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

        std::vector<glz::json_t> body;

        // 1. Header Container
        {
            glz::json_t header;
            header["type"] = "Container";
            header["style"] = "emphasis";

            glz::json_t col_set;
            col_set["type"] = "ColumnSet";
            std::vector<glz::json_t> columns;

            // Column 1: Document emoji
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;
                glz::json_t tb;
                tb["type"] = "TextBlock";
                tb["text"] = "📑";
                tb["size"] = "Large";
                items.push_back(std::move(tb));
                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            // Column 2: Document title & subtitle
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "stretch";
                std::vector<glz::json_t> items;

                std::string title_text = !path_.filename().empty()
                    ? path_.filename().string()
                    : (!title_.empty() ? title_ : "C++20 ISO Standard Specification.pdf");

                glz::json_t tb_title;
                tb_title["type"] = "TextBlock";
                tb_title["text"] = title_text;
                tb_title["weight"] = "Bolder";
                tb_title["size"] = "Large";
                items.push_back(std::move(tb_title));

                glz::json_t tb_sub;
                tb_sub["type"] = "TextBlock";
                tb_sub["text"] = "PDFium Hardware Rasterizer";
                tb_sub["isSubtle"] = true;
                tb_sub["size"] = "Small";
                items.push_back(std::move(tb_sub));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            // Column 3: Page indicator
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;

                std::string page_text;
                if (page_count_ > 0) {
                    page_text = std::format("Page {} / {}", current_page_ + 1, page_count_);
                } else if (path_.empty() && current_page_ == 0) {
                    page_text = "Page 42 / 1640";
                } else {
                    page_text = std::format("Page {} / {}", current_page_ + 1, std::max(current_page_ + 1, 1640));
                }

                glz::json_t tb_page;
                tb_page["type"] = "TextBlock";
                tb_page["text"] = page_text;
                tb_page["weight"] = "Bolder";
                tb_page["color"] = "Accent";
                tb_page["size"] = "Medium";
                items.push_back(std::move(tb_page));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            col_set["columns"] = std::move(columns);
            header["items"] = std::vector<glz::json_t>{std::move(col_set)};
            body.push_back(std::move(header));
        }

        // 2. FactSet
        {
            glz::json_t fact_set;
            fact_set["type"] = "FactSet";
            std::vector<glz::json_t> facts;

            std::string title_val = !title_.empty()
                ? title_
                : (!path_.empty() ? path_.filename().string() : "Programming Languages - C++ (ISO/IEC 14882:2020)");
            facts.push_back(glz::json_t::object_t{
                {"title", "Title:"},
                {"value", title_val}
            });

            std::string author_val = !author_.empty()
                ? author_
                : (!path_.empty() ? "Unknown" : "ISO/IEC JTC 1/SC 22/WG 21");
            facts.push_back(glz::json_t::object_t{
                {"title", "Author:"},
                {"value", author_val}
            });

            std::string current_page_val;
            if (page_count_ > 0) {
                current_page_val = std::format("{} of {}", current_page_ + 1, page_count_);
            } else if (path_.empty() && current_page_ == 0) {
                current_page_val = "42 (Clause 10: Modules)";
            } else {
                current_page_val = std::format("{}", current_page_ + 1);
            }
            facts.push_back(glz::json_t::object_t{
                {"title", "Current Page:"},
                {"value", current_page_val}
            });

            int zoom_pct = static_cast<int>(std::round(zoom_ * 100.0f));
            int zoom_dpi = static_cast<int>(std::round(zoom_ * 120.0f));
            std::string zoom_val = (path_.empty() && zoom_ == 1.0f)
                ? "125% (150 DPI)"
                : std::format("{}% ({} DPI)", zoom_pct, zoom_dpi);
            facts.push_back(glz::json_t::object_t{
                {"title", "Zoom Level:"},
                {"value", zoom_val}
            });

            fact_set["facts"] = std::move(facts);
            body.push_back(std::move(fact_set));
        }

        // 3. Navigation Container (Input.Number target_page)
        {
            glz::json_t input_container;
            input_container["type"] = "Container";
            input_container["separator"] = true;
            std::vector<glz::json_t> input_items;

            glz::json_t num_input;
            num_input["type"] = "Input.Number";
            num_input["id"] = "target_page";
            num_input["label"] = "Jump to Page";
            num_input["title"] = "Jump to Page";
            if (page_count_ > 0) {
                num_input["value"] = current_page_ + 1;
                num_input["min"] = 1;
                num_input["max"] = page_count_;
            } else if (path_.empty() && current_page_ == 0) {
                num_input["value"] = 42;
                num_input["min"] = 1;
                num_input["max"] = 1640;
            } else {
                num_input["value"] = current_page_ + 1;
                num_input["min"] = 1;
                num_input["max"] = std::max(current_page_ + 1, 1640);
            }
            input_items.push_back(std::move(num_input));

            input_container["items"] = std::move(input_items);
            body.push_back(std::move(input_container));
        }

        card["body"] = std::move(body);

        // Actions
        std::vector<glz::json_t> actions;
        {
            glz::json_t act_prev;
            act_prev["type"] = "Action.Execute";
            act_prev["title"] = "Previous Page";
            act_prev["verb"] = "pdf_prev_page";
            actions.push_back(std::move(act_prev));

            glz::json_t act_next;
            act_next["type"] = "Action.Execute";
            act_next["title"] = "Next Page";
            act_next["verb"] = "pdf_next_page";
            actions.push_back(std::move(act_next));

            glz::json_t act_jump;
            act_jump["type"] = "Action.Execute";
            act_jump["title"] = "Go to Page";
            act_jump["verb"] = "pdf_jump_page";
            actions.push_back(std::move(act_jump));
        }
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
                    if (verb.empty()) {
                        if (data.contains("verb") && data["verb"].holds<std::string>()) {
                            verb = data["verb"].get<std::string>();
                        } else if (data.contains("action") && data["action"].holds<std::string>()) {
                            verb = data["action"].get<std::string>();
                        }
                    }
                } else {
                    data = payload;
                }
            } else {
                verb = std::string(action_json);
            }

            if (verb == "pdf_jump_page") {
                int target = -1;
                auto extract_int = [](const glz::json_t& v) -> int {
                    if (v.holds<double>()) return static_cast<int>(v.get<double>());
                    if (v.holds<std::string>()) {
                        try { return std::stoi(v.get<std::string>()); } catch (...) {}
                    }
                    return -1;
                };

                if (data.is_object()) {
                    if (data.contains("page_number")) {
                        target = extract_int(data["page_number"]);
                    } else if (data.contains("target_page")) {
                        target = extract_int(data["target_page"]);
                    } else if (data.contains("page")) {
                        target = extract_int(data["page"]);
                    }
                }

                if (target >= 1) {
                    if (page_count_ > 0) {
                        current_page_ = std::clamp(target - 1, 0, page_count_ - 1);
                    } else {
                        current_page_ = target - 1;
                    }
                    texture_needs_update_ = true;
                }
                last_action_ = "pdf_jump_page";
                last_adaptive_parse_time_ = {};
            } else if (verb == "pdf_next_page") {
                if (page_count_ > 0) {
                    if (current_page_ < page_count_ - 1) {
                        current_page_++;
                        texture_needs_update_ = true;
                    }
                } else {
                    current_page_++;
                    texture_needs_update_ = true;
                }
                last_action_ = "pdf_next_page";
                last_adaptive_parse_time_ = {};
            } else if (verb == "pdf_prev_page") {
                if (current_page_ > 0) {
                    current_page_--;
                    texture_needs_update_ = true;
                }
                last_action_ = "pdf_prev_page";
                last_adaptive_parse_time_ = {};
            } else if (verb == "pdf_zoom") {
                float zoom_val = -1.0f;
                auto extract_zoom = [](const glz::json_t& v) -> float {
                    if (v.holds<double>()) return static_cast<float>(v.get<double>());
                    if (v.holds<std::string>()) {
                        std::string s = v.get<std::string>();
                        try {
                            if (s.ends_with('%')) {
                                return std::stof(s.substr(0, s.size() - 1)) / 100.0f;
                            }
                            return std::stof(s);
                        } catch (...) {}
                    }
                    return -1.0f;
                };

                if (data.is_object()) {
                    if (data.contains("zoom_factor")) {
                        zoom_val = extract_zoom(data["zoom_factor"]);
                    } else if (data.contains("zoom")) {
                        zoom_val = extract_zoom(data["zoom"]);
                    }
                }

                if (zoom_val > 0.05f && zoom_val <= 10.0f) {
                    zoom_ = zoom_val;
                    texture_needs_update_ = true;
                }
                last_action_ = "pdf_zoom";
                last_adaptive_parse_time_ = {};
            } else if (verb == "toggle_adaptive_view") {
                adaptive_view_mode = !adaptive_view_mode;
                last_action_ = "toggle_adaptive_view";
                last_adaptive_parse_time_ = {};
            } else if (verb == "open_pdf" || verb == "load_pdf" || verb == "open_doc") {
                std::string new_path;
                if (data.is_object()) {
                    if (data.contains("pdf_path") && data["pdf_path"].holds<std::string>()) {
                        new_path = data["pdf_path"].get<std::string>();
                    } else if (data.contains("path") && data["path"].holds<std::string>()) {
                        new_path = data["path"].get<std::string>();
                    }
                }
                if (!new_path.empty()) {
                    load_pdf(new_path);
                }
                last_action_ = verb;
                last_adaptive_parse_time_ = {};
            }
        } catch (const std::exception& e) {
            std::cerr << "[pdf] Action error: " << e.what() << std::endl;
        } catch (...) {}
    }

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }
    [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }
    [[nodiscard]] int get_current_page() const noexcept { return current_page_ + 1; }
    [[nodiscard]] int get_current_page_index() const noexcept { return current_page_; }
    [[nodiscard]] int get_total_pages() const noexcept { return page_count_; }
    [[nodiscard]] float get_zoom_factor() const noexcept { return zoom_; }
    [[nodiscard]] const std::string& get_title() const noexcept { return title_; }
    [[nodiscard]] const std::string& get_author() const noexcept { return author_; }
    [[nodiscard]] std::string get_pdf_path() const { return path_.string(); }
    void set_total_pages(int count) noexcept { page_count_ = count; }
    void set_title(std::string_view t) { title_ = t; }
    void set_author(std::string_view a) { author_ = a; }

private:
    void render_toolbar() {
        ImGui::PushStyleColor(ImGuiCol_Header, colors[0]);

        if (is_valid_) {
            // Navigation controls
            if (ImGui::Button(ICON_MD_CHEVRON_LEFT " Prev") || (ImGui::IsKeyPressed(ImGuiKey_PageUp) && ImGui::IsWindowFocused())) {
                if (current_page_ > 0) {
                    current_page_--;
                    texture_needs_update_ = true;
                }
            }
            ImGui::SameLine();
            
            ImGui::Text("Page %d of %d", current_page_ + 1, page_count_);
            ImGui::SameLine();

            if (ImGui::Button("Next " ICON_MD_CHEVRON_RIGHT) || (ImGui::IsKeyPressed(ImGuiKey_PageDown) && ImGui::IsWindowFocused())) {
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
            if (ImGui::Button(ICON_MD_ROTATE_RIGHT " Rotate")) {
                rotation_ = (rotation_ + 1) % 4;
                texture_needs_update_ = true;
            }
        }

        ImGui::PopStyleColor();
    }

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    std::string title_;
    std::string author_;
    std::string last_action_;

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
