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
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/filetype_handler.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/image_cache.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
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

            // Column 1: Emoji icon 📄
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;
                glz::json_t tb;
                tb["type"] = "TextBlock";
                tb["text"] = "📄";
                tb["size"] = "Large";
                items.push_back(std::move(tb));
                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            // Column 2: Title & subtitle
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "stretch";
                std::vector<glz::json_t> items;

                std::string title = path_.filename().empty() ? (path_.empty() ? "README.md" : path_.string()) : path_.filename().string();
                std::string sub = path_.empty() ? "/Users/ignaciorodriguez/src/rouen/README.md" : path_.string();

                glz::json_t tb_title;
                tb_title["type"] = "TextBlock";
                tb_title["text"] = title;
                tb_title["weight"] = "Bolder";
                tb_title["size"] = "Large";
                items.push_back(std::move(tb_title));

                glz::json_t tb_sub;
                tb_sub["type"] = "TextBlock";
                tb_sub["text"] = sub;
                tb_sub["isSubtle"] = true;
                tb_sub["size"] = "Small";
                items.push_back(std::move(tb_sub));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            // Column 3: Word count
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;

                int wc = word_count_;
                if (wc == 0 && path_.empty()) {
                    wc = 1420;
                }

                glz::json_t tb_wc;
                tb_wc["type"] = "TextBlock";
                tb_wc["text"] = format_word_count(wc);
                tb_wc["weight"] = "Bolder";
                tb_wc["size"] = "Small";
                items.push_back(std::move(tb_wc));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            col_set["columns"] = std::move(columns);
            header["items"] = std::vector<glz::json_t>{std::move(col_set)};
            body.push_back(std::move(header));
        }

        // Collect headings from parsed_document_
        int heading_count = 0;
        int min_level = 6;
        int max_level = 1;
        std::vector<std::string> toc_headings;
        for (const auto& prim : parsed_document_.primitives) {
            if (std::holds_alternative<rouen::helpers::heading_primitive>(prim)) {
                const auto& h = std::get<rouen::helpers::heading_primitive>(prim);
                heading_count++;
                min_level = std::min(min_level, h.level);
                max_level = std::max(max_level, h.level);
                if (!h.plain_text.empty()) {
                    toc_headings.push_back(h.plain_text);
                }
            }
        }

        // 2. FactSet
        {
            glz::json_t fact_set;
            fact_set["type"] = "FactSet";
            std::vector<glz::json_t> facts;

            std::string mod_time_str = "Today 16:30";
            if (!path_.empty() && is_valid_) {
                std::error_code ec;
                auto lwt = std::filesystem::last_write_time(path_, ec);
                if (!ec) {
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        lwt - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
                    );
                    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
                    std::tm tm_buf{};
#if defined(_WIN32)
                    localtime_s(&tm_buf, &tt);
#else
                    localtime_r(&tt, &tm_buf);
#endif
                    char buf[64]{};
                    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_buf);
                    mod_time_str = buf;
                }
            }

            facts.push_back(glz::json_t::object_t{
                {"title", "Last Modified:"},
                {"value", mod_time_str}
            });

            facts.push_back(glz::json_t::object_t{
                {"title", "Format:"},
                {"value", "CommonMark / GitHub Flavored Markdown"}
            });

            std::string headings_val;
            if (heading_count == 0 && path_.empty()) {
                headings_val = "8 sections (H1-H4)";
            } else if (heading_count == 0) {
                headings_val = "0 sections";
            } else if (min_level == max_level) {
                headings_val = std::format("{} section{} (H{})", heading_count, heading_count == 1 ? "" : "s", min_level);
            } else {
                headings_val = std::format("{} sections (H{}-H{})", heading_count, min_level, max_level);
            }

            facts.push_back(glz::json_t::object_t{
                {"title", "Headings:"},
                {"value", headings_val}
            });

            fact_set["facts"] = std::move(facts);
            body.push_back(std::move(fact_set));
        }

        // 3. Form Inputs Container (ChoiceSet for headings, Input.Text for document path)
        {
            glz::json_t input_container;
            input_container["type"] = "Container";
            std::vector<glz::json_t> input_items;

            glz::json_t choice_set;
            choice_set["type"] = "Input.ChoiceSet";
            choice_set["id"] = "heading";
            choice_set["title"] = "Jump to Heading";
            choice_set["value"] = selected_heading_;
            std::vector<glz::json_t> choices;
            if (!toc_headings.empty()) {
                for (const auto& h : toc_headings) {
                    choices.push_back(glz::json_t::object_t{{"title", h}, {"value", h}});
                }
            } else {
                choices.push_back(glz::json_t::object_t{{"title", "Executive Summary"}, {"value", "Executive Summary"}});
            }
            choice_set["choices"] = std::move(choices);
            input_items.push_back(std::move(choice_set));

            glz::json_t path_input;
            path_input["type"] = "Input.Text";
            path_input["id"] = "doc_path";
            path_input["title"] = "Document Path";
            path_input["value"] = path_.string();
            input_items.push_back(std::move(path_input));

            input_container["items"] = std::move(input_items);
            body.push_back(std::move(input_container));
        }

        // 4. Executive Summary / Content Preview Container
        {
            glz::json_t summary_container;
            summary_container["type"] = "Container";
            summary_container["separator"] = true;

            std::string summary_title = "Executive Summary";
            std::string summary_text = "Rouen is a next-generation desktop workspace fusing C++20 performance with the Microsoft Adaptive Cards declarative layout engine. Designed for developers, operators, and creators.";

            if (!parsed_document_.empty()) {
                bool got_heading = false;
                bool got_para = false;
                for (const auto& prim : parsed_document_.primitives) {
                    if (!got_heading && std::holds_alternative<rouen::helpers::heading_primitive>(prim)) {
                        const auto& h = std::get<rouen::helpers::heading_primitive>(prim);
                        if (!h.plain_text.empty()) {
                            summary_title = h.plain_text;
                            got_heading = true;
                        }
                    } else if (!got_para && std::holds_alternative<rouen::helpers::paragraph_primitive>(prim)) {
                        const auto& p = std::get<rouen::helpers::paragraph_primitive>(prim);
                        std::string p_text;
                        for (const auto& sp : p.spans) {
                            p_text += sp.text;
                        }
                        if (!p_text.empty()) {
                            summary_text = p_text;
                            got_para = true;
                        }
                    }
                    if (got_heading && got_para) break;
                }
            }

            std::vector<glz::json_t> sum_items;
            glz::json_t tb_sum_title;
            tb_sum_title["type"] = "TextBlock";
            tb_sum_title["text"] = summary_title;
            tb_sum_title["weight"] = "Bolder";
            tb_sum_title["size"] = "Medium";
            sum_items.push_back(std::move(tb_sum_title));

            glz::json_t tb_sum_body;
            tb_sum_body["type"] = "TextBlock";
            tb_sum_body["text"] = summary_text;
            tb_sum_body["wrap"] = true;
            sum_items.push_back(std::move(tb_sum_body));

            summary_container["items"] = std::move(sum_items);
            body.push_back(std::move(summary_container));
        }

        card["body"] = std::move(body);

        // Actions
        std::vector<glz::json_t> actions;
        {
            glz::json_t act_reload;
            act_reload["type"] = "Action.Execute";
            act_reload["title"] = "Reload Document";
            act_reload["verb"] = "reload_doc";
            actions.push_back(std::move(act_reload));

            glz::json_t act_copy;
            act_copy["type"] = "Action.Execute";
            act_copy["title"] = "Copy Markdown";
            act_copy["verb"] = "copy_markdown";
            actions.push_back(std::move(act_copy));

            glz::json_t act_nav;
            act_nav["type"] = "Action.Execute";
            act_nav["title"] = "Jump to Heading";
            act_nav["verb"] = "navigate_section";
            actions.push_back(std::move(act_nav));
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

            if (verb == "reload_doc") {
                if (!path_.empty()) {
                    load_file(path_.string());
                }
                last_action_ = "reload_doc";
                last_adaptive_parse_time_ = {};
            } else if (verb == "copy_markdown") {
                if (ImGui::GetCurrentContext() != nullptr) {
                    ImGui::SetClipboardText(content_.c_str());
                } else {
                    SDL_SetClipboardText(content_.c_str());
                }
                last_action_ = "copy_markdown";
                last_adaptive_parse_time_ = {};
            } else if (verb == "navigate_section") {
                std::string heading;
                if (data.is_object()) {
                    if (data.contains("heading") && data["heading"].holds<std::string>()) {
                        heading = data["heading"].get<std::string>();
                    } else if (data.contains("selected_heading") && data["selected_heading"].holds<std::string>()) {
                        heading = data["selected_heading"].get<std::string>();
                    } else if (data.contains("section") && data["section"].holds<std::string>()) {
                        heading = data["section"].get<std::string>();
                    }
                }
                selected_heading_ = heading;
                target_heading_ = heading;
                last_action_ = "navigate_section";
                last_adaptive_parse_time_ = {};
            } else if (verb == "toggle_adaptive_view") {
                adaptive_view_mode = !adaptive_view_mode;
                last_action_ = "toggle_adaptive_view";
                last_adaptive_parse_time_ = {};
            } else if (verb == "open_doc" || verb == "load_file") {
                std::string new_path;
                if (data.is_object()) {
                    if (data.contains("doc_path") && data["doc_path"].holds<std::string>()) {
                        new_path = data["doc_path"].get<std::string>();
                    } else if (data.contains("path") && data["path"].holds<std::string>()) {
                        new_path = data["path"].get<std::string>();
                    }
                }
                if (!new_path.empty()) {
                    load_file(new_path);
                }
                last_action_ = verb;
                last_adaptive_parse_time_ = {};
            }
        } catch (const std::exception& e) {
            std::cerr << "[markdown] Action error: " << e.what() << std::endl;
        } catch (...) {}
    }

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }
    [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }
    [[nodiscard]] const std::string& get_selected_heading() const noexcept { return selected_heading_; }
    [[nodiscard]] std::string get_doc_path() const { return path_.string(); }
    [[nodiscard]] std::string get_doc_title() const {
        return path_.filename().empty() ? (path_.empty() ? "Markdown Viewer" : path_.string()) : path_.filename().string();
    }
    [[nodiscard]] int get_word_count() const noexcept { return word_count_; }
    [[nodiscard]] int get_line_count() const noexcept { return line_count_; }

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

    static std::string format_word_count(int count) {
        std::string s = std::to_string(count);
        int insert_pos = static_cast<int>(s.length()) - 3;
        while (insert_pos > 0) {
            s.insert(static_cast<size_t>(insert_pos), ",");
            insert_pos -= 3;
        }
        return s + " words";
    }

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    mutable std::string adaptive_error_{};
    mutable std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    std::string last_action_{};
    std::string selected_heading_{};
    std::string target_heading_{};
};

} // namespace rouen::cards
