#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>

#include "../../../external/IconsMaterialDesign.h"
#include "../../fonts.hpp"
#include "../../helpers/filetype_handler.hpp"
#include "../../helpers/image_cache.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/markdown_renderer.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/string_helper.hpp"
#include "../../helpers/texture_helper.hpp"
#include "../../helpers/texture_utils.hpp"
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

    ~markdown_viewer() override {
        clear_image_cache();
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
        clear_image_cache();
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

        parsed_document_ = rouen::helpers::parse_markdown_document(content_);
        line_count_ = parsed_document_.line_count;
        word_count_ = parsed_document_.word_count;

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
                if (parsed_document_.empty()) {
                    ImGui::TextDisabled("(Empty document)");
                } else {
                    const rouen::helpers::markdown_render_config md_config{
                        .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                        .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono),
                        .render_image_cb = [this](const std::string& alt, const std::string& url) {
                            render_image(alt, url);
                        },
                    };
                    rouen::helpers::render_markdown_document(
                        parsed_document_,
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

    void render_image(const std::string& alt, const std::string& url) {
        if (url.empty()) return;

        auto it = image_textures_.find(url);
        if (it == image_textures_.end()) {
            cached_image_entry entry{};

            if (url.starts_with("http://") || url.starts_with("https://")) {
                if (!remote_image_cache_) {
                    auto cache_dir = (rouen::platform::get_user_data_path() / "images").string();
                    auto db_path = (rouen::platform::get_user_data_path("image_cache.db", true)).string();
                    remote_image_cache_ = std::make_shared<::helpers::ImageCache>(db_path, cache_dir, 30);
                }
                int w = 0, h = 0;
                if (remote_image_cache_->isCached(url, w, h)) {
                    entry.texture = remote_image_cache_->getTexture(TextureHelper::g_gpu_device, url, w, h);
                    entry.width = w;
                    entry.height = h;
                    entry.loaded = (entry.texture != nullptr);
                } else {
                    std::lock_guard<std::mutex> lock(image_download_mutex_);
                    if (!pending_image_downloads_.contains(url)) {
                        pending_image_downloads_.insert(url);
                        std::thread([this, url]() {
                            try {
                                if (remote_image_cache_) {
                                    remote_image_cache_->downloadAndCache(url);
                                }
                            } catch (...) {}
                            std::lock_guard<std::mutex> lock2(image_download_mutex_);
                            pending_image_downloads_.erase(url);
                        }).detach();
                    }
                }
            } else {
                std::string clean_path = url;
                if (clean_path.starts_with("file://")) {
                    clean_path = clean_path.substr(7);
                }
                std::filesystem::path p(clean_path);
                std::filesystem::path target;
                if (p.is_absolute()) {
                    target = p;
                } else if (!path_.empty()) {
                    target = (path_.parent_path() / p).lexically_normal();
                } else {
                    target = p.lexically_normal();
                }

                std::error_code ec;
                if (std::filesystem::exists(target, ec)) {
                    int w = 0, h = 0;
                    entry.texture = TextureHelper::loadTextureFromFile(
                        TextureHelper::g_gpu_device,
                        target.string().c_str(),
                        w, h
                    );
                    entry.width = w;
                    entry.height = h;
                    entry.loaded = (entry.texture != nullptr);
                    entry.load_failed = !entry.loaded;
                } else {
                    entry.loaded = false;
                    entry.load_failed = true;
                }
            }

            auto [ins, _] = image_textures_.emplace(url, entry);
            it = ins;
        }

        // Retry check for remote image that might have just finished caching
        if (!it->second.loaded && !it->second.load_failed && remote_image_cache_ &&
            (url.starts_with("http://") || url.starts_with("https://"))) {
            int w = 0, h = 0;
            if (remote_image_cache_->isCached(url, w, h)) {
                it->second.texture = remote_image_cache_->getTexture(TextureHelper::g_gpu_device, url, w, h);
                it->second.width = w;
                it->second.height = h;
                it->second.loaded = (it->second.texture != nullptr);
            }
        }

        if (it->second.loaded && it->second.texture) {
            float const avail_w = ImGui::GetContentRegionAvail().x;
            float const orig_w = static_cast<float>(it->second.width);
            float const orig_h = static_cast<float>(it->second.height);

            float draw_w = orig_w;
            float draw_h = orig_h;
            if (avail_w > 10.0f && orig_w > avail_w) {
                float const scale = avail_w / orig_w;
                draw_w = avail_w;
                draw_h = orig_h * scale;
            }

            ImGui::Spacing();
            ImTextureID const tex_id = rouen::helpers::texture_id_cast(it->second.texture);
            ImGui::Image(tex_id, ImVec2(draw_w, draw_h));

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s (Click to open)", alt.empty() ? url.c_str() : alt.c_str());
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            if (ImGui::IsItemClicked()) {
                handle_link_click(url);
            }

            if (!alt.empty()) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.60f, 0.65f, 1.0f));
                ImGui::TextDisabled("▲ %s", alt.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
            if (it->second.load_failed) {
                ImGui::TextDisabled("[Image not found: %s]", url.c_str());
            } else {
                ImGui::TextDisabled("[Loading image: %s...]", url.c_str());
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Image target: %s", url.c_str());
            }
            if (ImGui::IsItemClicked()) {
                handle_link_click(url);
            }
        }
    }

    void clear_image_cache() {
        for (auto& [url, img] : image_textures_) {
            if (img.texture) {
                TextureHelper::destroyTexture(img.texture);
                img.texture = nullptr;
            }
        }
        image_textures_.clear();
    }

    struct cached_image_entry {
        RouenGPUTexture* texture{nullptr};
        int width{0};
        int height{0};
        bool loaded{false};
        bool load_failed{false};
    };
    std::unordered_map<std::string, cached_image_entry> image_textures_;
    std::shared_ptr<::helpers::ImageCache> remote_image_cache_;
    std::set<std::string> pending_image_downloads_;
    std::mutex image_download_mutex_;

    std::filesystem::path path_;
    std::string content_;
    rouen::helpers::markdown_document parsed_document_;
    std::string size_str_;
    int line_count_{0};
    int word_count_{0};
    bool is_valid_{false};
    std::string status_message_;
    std::filesystem::file_time_type last_write_time_{};
    std::chrono::steady_clock::time_point last_check_time_{};
};

} // namespace rouen::cards
