#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include "../../../external/IconsMaterialDesign.h"
#include "../../fonts.hpp"
#include "../../helpers/filetype_handler.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/markdown_renderer.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../registrar.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

class markdown_viewer : public card {
public:
    explicit markdown_viewer(std::string_view locator = "") {
        // Markdown viewer color accents (Markdown blue/slate theme)
        colors[0] = ImVec4(0.24f, 0.52f, 0.78f, 1.0f); // Primary blue
        colors[1] = ImVec4(0.35f, 0.65f, 0.90f, 0.7f); // Secondary accent
        get_color(2, ImVec4(0.18f, 0.60f, 0.85f, 1.0f)); // Header accent
        get_color(3, ImVec4(0.60f, 0.60f, 0.60f, 1.0f)); // Dim text

        width = 750.0f;
        requested_fps = 10;

        if (!locator.empty()) {
            std::string path_str = ::helpers::StringHelper::url_decode(locator);
            load_file(path_str);
        } else {
            name("Markdown Viewer");
        }
    }

    [[nodiscard]] std::string get_uri() const override {
        return std::format("markdown:{}", path_.string());
    }

    [[nodiscard]] bool matches_uri(std::string_view uri) const override {
        return uri == "markdown" || uri.starts_with("markdown:") ||
               uri == "md"       || uri.starts_with("md:");
    }

    void handle_uri(std::string_view uri) override {
        if (uri.starts_with("markdown:")) {
            load_file(::helpers::StringHelper::url_decode(uri.substr(9)));
        } else if (uri.starts_with("md:")) {
            load_file(::helpers::StringHelper::url_decode(uri.substr(3)));
        }
    }

    void load_file(const std::string& filepath) {
        path_ = std::filesystem::path(filepath);
        name(path_.filename().empty() ? "Markdown Viewer" : path_.filename().string());

        if (filepath.empty()) {
            status_message_ = "No file specified.";
            content_.clear();
            is_valid_ = false;
            return;
        }

        std::error_code ec;
        if (!std::filesystem::exists(path_, ec)) {
            status_message_ = std::format("File not found: {}", filepath);
            content_.clear();
            is_valid_ = false;
            return;
        }

        std::ifstream file(path_);
        if (!file.is_open()) {
            status_message_ = std::format("Could not open file: {}", filepath);
            content_.clear();
            is_valid_ = false;
            return;
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        content_ = ss.str();
        is_valid_ = true;
        status_message_.clear();

        last_write_time_ = std::filesystem::last_write_time(path_, ec);
        last_check_time_ = std::chrono::steady_clock::now();

        // Calculate statistics
        line_count_ = 0;
        word_count_ = 0;
        bool in_word = false;
        for (char c : content_) {
            if (c == '\n') ++line_count_;
            if (std::isspace(static_cast<unsigned char>(c))) {
                in_word = false;
            } else if (!in_word) {
                in_word = true;
                ++word_count_;
            }
        }
        if (!content_.empty() && content_.back() != '\n') ++line_count_;

        auto bytes = std::filesystem::file_size(path_, ec);
        if (!ec) {
            if (bytes < 1024) {
                size_str_ = std::format("{} B", bytes);
            } else if (bytes < 1024 * 1024) {
                size_str_ = std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
            } else {
                size_str_ = std::format("{:.2f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
            }
        }
    }

    bool render() override {
        return render_window([this]() {
            handle_shortcuts();
            check_auto_reload();
            render_toolbar();

            if (!is_valid_) {
                ImGui::Separator();
                ImGui::Spacing();
                if (!status_message_.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", status_message_.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No markdown file currently opened.");
                }
                ImGui::Spacing();

                static char path_buf[512] = "";
                ImGui::Text("Open Markdown File Path:");
                ImGui::InputText("##md_open_path", path_buf, sizeof(path_buf));
                ImGui::SameLine();
                if (ImGui::Button("Open")) {
                    load_file(path_buf);
                }
                return;
            }

            if (ImGui::BeginChild("MarkdownViewerContent", ImVec2(0.0f, 0.0f), true)) {
                if (content_.empty()) {
                    ImGui::TextDisabled("(Empty document)");
                } else {
                    const rouen::helpers::markdown_render_config md_config{
                        .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                        .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono),
                    };
                    rouen::helpers::render_markdown_block(
                        content_,
                        md_config,
                        [this](const std::string& url) {
                            handle_link_click(url);
                        }
                    );
                }
            }
            ImGui::EndChild();
        });
    }

private:
    void render_toolbar() {
        ImGui::PushStyleColor(ImGuiCol_Header, colors[0]);

        if (is_valid_) {
            if (ImGui::Button(ICON_MD_EDIT " Edit")) {
                "edit"_sfn(path_.string());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Open in text editor (Ctrl+E / Cmd+E)");
            }
            ImGui::SameLine();

            if (ImGui::Button(ICON_MD_REFRESH " Reload")) {
                load_file(path_.string());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reload file from disk (Ctrl+R / Cmd+R)");
            }
            ImGui::SameLine();

            if (ImGui::Button(ICON_MD_CONTENT_COPY "##copy_md_path")) {
                ImGui::SetClipboardText(path_.string().c_str());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Copy file path to clipboard");
            }
            ImGui::SameLine();

            ImGui::TextDisabled("| %s  •  %d lines  •  %d words", size_str_.c_str(), line_count_, word_count_);
            ImGui::Separator();
        }

        ImGui::PopStyleColor();
    }

    void handle_shortcuts() {
        auto& io = ImGui::GetIO();
        const bool ctrl_or_cmd = io.KeyCtrl || io.KeySuper;
        if (!ctrl_or_cmd || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            if (is_valid_ && !path_.empty()) {
                "edit"_sfn(path_.string());
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            if (!path_.empty()) {
                load_file(path_.string());
            }
        }
    }

    void check_auto_reload() {
        if (!is_valid_ || path_.empty()) return;
        auto now = std::chrono::steady_clock::now();
        if (now - last_check_time_ < std::chrono::seconds(2)) return;
        last_check_time_ = now;

        std::error_code ec;
        if (std::filesystem::exists(path_, ec)) {
            auto current_lwt = std::filesystem::last_write_time(path_, ec);
            if (!ec && current_lwt != last_write_time_) {
                load_file(path_.string());
            }
        }
    }

    void handle_link_click(const std::string& url) {
        if (url.starts_with("http://") || url.starts_with("https://") || url.starts_with("mailto:")) {
            rouen::platform::open_url(url);
            return;
        }

        if (url.starts_with("notes:") || url.starts_with("dir:") || url.starts_with("terminal:") ||
            url.starts_with("pdf:") || url.starts_with("image:") || url.starts_with("media:")) {
            "create_card"_sfn(url);
            return;
        }

        // Relative path resolution against parent directory of current markdown document
        std::filesystem::path target = path_.parent_path() / url;
        std::error_code ec;
        if (std::filesystem::exists(target, ec)) {
            std::string ext = target.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (ext == ".md" || ext == ".markdown") {
                load_file(target.lexically_normal().string());
                return;
            } else {
                auto resolved_uri = helpers::FiletypeHandler::instance().resolve(target, false);
                if (resolved_uri.has_value()) {
                    "create_card"_sfn(resolved_uri.value());
                } else {
                    "edit"_sfn(target.string());
                }
                return;
            }
        }

        // Fallback: try opening as generic system URL
        rouen::platform::open_url(url);
    }

    std::filesystem::path path_;
    std::string content_;
    std::string size_str_;
    int line_count_{0};
    int word_count_{0};
    bool is_valid_{false};
    std::string status_message_;
    std::filesystem::file_time_type last_write_time_{};
    std::chrono::steady_clock::time_point last_check_time_{};
};

} // namespace rouen::cards
