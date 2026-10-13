#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <format>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <TextEditor.h>

#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/markdown_renderer.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../models/notes/notes_repository.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

class markdown_notes : public card {
public:
    markdown_notes(std::string_view locator = {}, const std::string& db_path = {})
        : repository_{db_path.empty() ? rouen::platform::get_user_data_path("notes.db").string() : db_path} {
        colors[0] = {0.22f, 0.42f, 0.65f, 1.0f};
        colors[1] = {0.28f, 0.51f, 0.76f, 0.72f};
        get_color(2, ImVec4{0.86f, 0.55f, 0.25f, 1.0f});
        get_color(3, ImVec4{0.28f, 0.74f, 0.37f, 1.0f});
        get_color(4, ImVec4{0.93f, 0.33f, 0.25f, 1.0f});

        width = 1100.0f;
        requested_fps = 10;
        name("Markdown Notes");

        editor_.SetShowWhitespaces(false);
        editor_.SetTabSize(4);
        editor_.SetLanguageDefinition(markdown_language_definition());
        editor_.SetPalette(editor_.GetDarkPalette());

        sync_repo_url_ = repository_.get_sync_meta("notes_sync_repo_url");
        sync_cache_path_ = repository_.get_sync_meta(
            "notes_sync_cache_path",
            rouen::platform::get_user_data_path("notes-sync", true).string()
        );
        last_sync_timestamp_ = repository_.get_sync_meta("notes_last_sync");

        refresh_notes();
        if (!locator.empty()) {
            handle_uri(std::format("notes:{}", locator));
        } else if (!notes_cache_.empty()) {
            load_note(notes_cache_.front());
        }
    }

    std::string get_uri() const override {
        if (selected_note_title_.empty()) {
            return "notes";
        }
        return std::format("notes:{}", selected_note_title_);
    }

    bool matches_uri(std::string_view uri) const override {
        return uri == "notes" || uri.starts_with("notes:");
    }

    void handle_uri(std::string_view uri) override {
        std::string target;
        if (uri.starts_with("notes:")) {
            target = models::notes::notes_repository::trim(uri.substr(6));
        }

        if (target.empty()) {
            return;
        }

        auto note = repository_.get_note_by_title(target);
        if (note.has_value()) {
            load_note(*note);
            return;
        }

        clear_editor_for_new_note();
        set_title_buffer(target);
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
            render_status_bar();

            if (ImGui::BeginTabBar("MarkdownNotesTabs", ImGuiTabBarFlags_None)) {
                if (ImGui::BeginTabItem("Editor")) {
                    render_editor_tab();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Notes List")) {
                    render_notes_tab();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Backlinks")) {
                    render_backlinks_tab();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Sync")) {
                    render_sync_tab();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
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
            glz::json_t header_container;
            header_container["type"] = "Container";
            header_container["style"] = "emphasis";

            glz::json_t col_set;
            col_set["type"] = "ColumnSet";
            std::vector<glz::json_t> columns;

            // Column 1: Emoji memo icon
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;
                glz::json_t tb;
                tb["type"] = "TextBlock";
                tb["text"] = "📝";
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

                glz::json_t tb_title;
                tb_title["type"] = "TextBlock";
                tb_title["text"] = "Markdown Notes Studio";
                tb_title["weight"] = "Bolder";
                tb_title["size"] = "Large";
                items.push_back(std::move(tb_title));

                glz::json_t tb_sub;
                tb_sub["type"] = "TextBlock";
                tb_sub["text"] = "Organized personal knowledge base";
                tb_sub["isSubtle"] = true;
                tb_sub["size"] = "Small";
                items.push_back(std::move(tb_sub));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            // Column 3: Tag badge
            {
                glz::json_t col;
                col["type"] = "Column";
                col["width"] = "auto";
                std::vector<glz::json_t> items;

                std::string tag_display;
                std::string raw_tags = tags_buffer_.data();
                if (!raw_tags.empty()) {
                    std::stringstream ss(raw_tags);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        auto trimmed = models::notes::notes_repository::trim(item);
                        if (!trimmed.empty()) {
                            if (!tag_display.empty()) tag_display += " ";
                            if (!trimmed.starts_with("#")) tag_display += "#";
                            tag_display += trimmed;
                        }
                    }
                }
                if (tag_display.empty()) {
                    tag_display = "#notes #mesh";
                }

                glz::json_t tb_tags;
                tb_tags["type"] = "TextBlock";
                tb_tags["text"] = tag_display;
                tb_tags["weight"] = "Bolder";
                tb_tags["color"] = "Accent";
                tb_tags["size"] = "Small";
                items.push_back(std::move(tb_tags));

                col["items"] = std::move(items);
                columns.push_back(std::move(col));
            }

            col_set["columns"] = std::move(columns);
            header_container["items"] = std::vector<glz::json_t>{std::move(col_set)};
            body.push_back(std::move(header_container));
        }

        // 2. FactSet
        {
            glz::json_t fact_set;
            fact_set["type"] = "FactSet";
            std::vector<glz::json_t> facts;

            facts.push_back(glz::json_t::object_t{
                {"title", "Note ID:"},
                {"value", std::to_string(selected_note_id_)}
            });

            facts.push_back(glz::json_t::object_t{
                {"title", "Status:"},
                {"value", editor_.IsTextChanged() ? "Unsaved" : "Saved"}
            });

            facts.push_back(glz::json_t::object_t{
                {"title", "Total Notes:"},
                {"value", std::to_string(notes_cache_.size())}
            });

            facts.push_back(glz::json_t::object_t{
                {"title", "Last Sync:"},
                {"value", last_sync_timestamp_.empty() ? "never" : last_sync_timestamp_}
            });

            fact_set["facts"] = std::move(facts);
            body.push_back(std::move(fact_set));
        }

        // 3. Form Inputs Container
        {
            glz::json_t input_container;
            input_container["type"] = "Container";

            std::vector<glz::json_t> items;

            // Note Title Input
            {
                glz::json_t input_title;
                input_title["type"] = "Input.Text";
                input_title["id"] = "note_title";
                input_title["label"] = "Note Title";
                std::string cur_title = title_buffer_.data();
                input_title["value"] = cur_title.empty() ? selected_note_title_ : cur_title;
                items.push_back(std::move(input_title));
            }

            // Note Tags Input
            {
                glz::json_t input_tags;
                input_tags["type"] = "Input.Text";
                input_tags["id"] = "note_tags";
                input_tags["label"] = "Tags (comma-separated)";
                input_tags["value"] = std::string(tags_buffer_.data());
                items.push_back(std::move(input_tags));
            }

            // Note Content Input
            {
                glz::json_t input_content;
                input_content["type"] = "Input.Text";
                input_content["id"] = "note_content";
                input_content["label"] = "Markdown Content";
                input_content["isMultiline"] = true;
                input_content["value"] = get_current_note_content();
                items.push_back(std::move(input_content));
            }

            input_container["items"] = std::move(items);
            body.push_back(std::move(input_container));
        }

        card["body"] = std::move(body);

        // Actions
        std::vector<glz::json_t> actions;
        {
            glz::json_t act_save;
            act_save["type"] = "Action.Execute";
            act_save["title"] = "Save Note";
            act_save["verb"] = "save_note";
            actions.push_back(std::move(act_save));

            glz::json_t act_new;
            act_new["type"] = "Action.Execute";
            act_new["title"] = "New Note";
            act_new["verb"] = "new_note";
            actions.push_back(std::move(act_new));

            glz::json_t act_delete;
            act_delete["type"] = "Action.Execute";
            act_delete["title"] = "Delete Note";
            act_delete["verb"] = "delete_note";
            actions.push_back(std::move(act_delete));
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
            glz::json_t payload{};
            std::string verb;
            glz::json_t data{};

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

            if (verb == "save_note") {
                if (data.is_object()) {
                    if (data.contains("title") && data["title"].holds<std::string>()) {
                        set_title_buffer(data["title"].get<std::string>());
                    } else if (data.contains("note_title") && data["note_title"].holds<std::string>()) {
                        set_title_buffer(data["note_title"].get<std::string>());
                    }

                    if (data.contains("tags") && data["tags"].holds<std::string>()) {
                        set_tags_buffer(data["tags"].get<std::string>());
                    } else if (data.contains("note_tags") && data["note_tags"].holds<std::string>()) {
                        set_tags_buffer(data["note_tags"].get<std::string>());
                    }

                    if (data.contains("content") && data["content"].holds<std::string>()) {
                        editor_.SetText(data["content"].get<std::string>());
                    } else if (data.contains("note_content") && data["note_content"].holds<std::string>()) {
                        editor_.SetText(data["note_content"].get<std::string>());
                    }
                }
                save_current_note();
                last_action_ = "save_note";
                last_adaptive_parse_time_ = {};
            } else if (verb == "new_note") {
                clear_editor_for_new_note();
                status_message_ = "New note buffer initialized";
                last_action_ = "new_note";
                last_adaptive_parse_time_ = {};
            } else if (verb == "delete_note") {
                int note_id = 0;
                if (data.is_object()) {
                    if (data.contains("note_id")) {
                        if (data["note_id"].holds<std::string>()) {
                            try { note_id = std::stoi(data["note_id"].get<std::string>()); } catch (...) {}
                        } else if (data["note_id"].holds<double>()) {
                            note_id = static_cast<int>(data["note_id"].get<double>());
                        }
                    } else if (data.contains("id")) {
                        if (data["id"].holds<std::string>()) {
                            try { note_id = std::stoi(data["id"].get<std::string>()); } catch (...) {}
                        } else if (data["id"].holds<double>()) {
                            note_id = static_cast<int>(data["id"].get<double>());
                        }
                    }
                }
                if (note_id <= 0) {
                    note_id = selected_note_id_;
                }
                if (note_id > 0) {
                    if (repository_.delete_note(note_id)) {
                        status_message_ = "Note deleted";
                        if (selected_note_id_ == note_id) {
                            clear_editor_for_new_note();
                        }
                        refresh_notes();
                    }
                }
                last_action_ = "delete_note";
                last_adaptive_parse_time_ = {};
            } else if (verb == "search_notes") {
                std::string query;
                if (data.is_object()) {
                    if (data.contains("query") && data["query"].holds<std::string>()) {
                        query = data["query"].get<std::string>();
                    } else if (data.contains("search_term") && data["search_term"].holds<std::string>()) {
                        query = data["search_term"].get<std::string>();
                    } else if (data.contains("search") && data["search"].holds<std::string>()) {
                        query = data["search"].get<std::string>();
                    }

                    if (data.contains("tag_filter") && data["tag_filter"].holds<std::string>()) {
                        set_buffer(tag_filter_buffer_, data["tag_filter"].get<std::string>());
                    }
                }
                set_buffer(search_buffer_, query);
                notes_cache_ = repository_.list_notes(
                    models::notes::notes_repository::trim(search_buffer_.data()),
                    models::notes::notes_repository::trim(tag_filter_buffer_.data())
                );
                last_action_ = "search_notes";
                last_adaptive_parse_time_ = {};
            } else if (verb == "toggle_adaptive_view") {
                adaptive_view_mode = !adaptive_view_mode;
                last_adaptive_parse_time_ = {};
                last_action_ = "toggle_adaptive_view";
            }
        } catch (const std::exception& e) {
            std::cerr << "[notes] Action error: " << e.what() << std::endl;
        } catch (...) {}
    }

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }

    [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }
    [[nodiscard]] int get_current_note_id() const noexcept { return selected_note_id_; }
    [[nodiscard]] std::string get_current_note_title() const {
        std::string s = title_buffer_.data();
        return s.empty() ? selected_note_title_ : s;
    }
    [[nodiscard]] std::string get_current_note_content() const {
        std::string text = editor_.GetText();
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }
        return text;
    }
    [[nodiscard]] std::string get_current_tags() const { return std::string(tags_buffer_.data()); }
    [[nodiscard]] std::string get_search_query() const { return std::string(search_buffer_.data()); }
    [[nodiscard]] size_t get_notes_count() const noexcept { return notes_cache_.size(); }
    [[nodiscard]] bool is_dirty() const { return editor_.IsTextChanged(); }

private:
    void render_status_bar() {
        const bool unsaved = editor_.IsTextChanged();

        ImGui::TextColored(colors[2], "%s", "Markdown knowledge base");
        ImGui::SameLine();
        ImGui::Text("| Active: %s", selected_note_title_.empty() ? "(new note)" : selected_note_title_.c_str());
        ImGui::SameLine();
        ImGui::TextColored(unsaved ? colors[4] : colors[3], "%s", unsaved ? "● Unsaved" : "● Saved");
        ImGui::SameLine();
        if (last_sync_timestamp_.empty()) {
            ImGui::Text("| Last sync: never");
        } else {
            ImGui::Text("| Last sync: %s", last_sync_timestamp_.c_str());
        }

        if (!status_message_.empty()) {
            ImGui::TextWrapped("%s", status_message_.c_str());
        }

        ImGui::Separator();
    }

    void render_editor_tab() {
        ImGui::InputText("Title", title_buffer_.data(), title_buffer_.size());
        ImGui::InputText("Tags (comma separated)", tags_buffer_.data(), tags_buffer_.size());

        if (ImGui::Button("New")) {
            clear_editor_for_new_note();
        }
        ImGui::SameLine();

        if (ImGui::Button("Save")) {
            save_current_note();
        }
        ImGui::SameLine();

        bool can_delete = selected_note_id_ > 0;
        if (!can_delete) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Delete")) {
            if (repository_.delete_note(selected_note_id_)) {
                status_message_ = "Note deleted";
                clear_editor_for_new_note();
                refresh_notes();
            }
        }
        if (!can_delete) {
            ImGui::EndDisabled();
        }

        ImGui::Separator();

        const auto available = ImGui::GetContentRegionAvail();
        const float editor_width = available.x * 0.58f;
        const float preview_width = available.x - editor_width - 8.0f;

        ImGui::BeginChild("MarkdownEditorPane", ImVec2(editor_width, 0.0f), true, ImGuiWindowFlags_NoNavInputs);
        editor_.Render("##markdown_editor", ImGui::GetContentRegionAvail(), true);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("MarkdownPreviewPane", ImVec2(preview_width, 0.0f), true);
        ImGui::TextColored(colors[2], "%s", "Live Preview");
        ImGui::Separator();
        {
            const rouen::helpers::markdown_render_config md_config{
                .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono),
            };
            rouen::helpers::render_markdown_block(
                editor_.GetText(),
                md_config,
                [this](const std::string& url) {
                    if (url.starts_with("notes:")) {
                        handle_uri(url);
                    } else {
                        rouen::platform::open_url(url);
                    }
                }
            );
        }
        ImGui::Spacing();

        const auto links = models::notes::notes_repository::parse_wiki_links(editor_.GetText());
        if (!links.empty()) {
            ImGui::Separator();
            ImGui::TextColored(colors[2], "%s", "Wiki Links");
            for (const auto& link : links) {
                if (ImGui::SmallButton(std::format("[[{}]]", link).c_str())) {
                    handle_uri(std::format("notes:{}", link));
                }
            }
        }
        ImGui::EndChild();
    }

    void render_notes_tab() {
        ImGui::InputText("Search", search_buffer_.data(), search_buffer_.size());
        ImGui::SameLine();
        ImGui::InputText("Tag filter", tag_filter_buffer_.data(), tag_filter_buffer_.size());

        if (ImGui::Button("Refresh")) {
            refresh_notes();
        }

        auto notes = repository_.list_notes(
            models::notes::notes_repository::trim(search_buffer_.data()),
            models::notes::notes_repository::trim(tag_filter_buffer_.data())
        );

        ImGui::Separator();
        ImGui::Text("%d note(s)", static_cast<int>(notes.size()));

        ImGui::BeginChild("NotesListPane", ImVec2(0.0f, 0.0f), true);
        for (const auto& note : notes) {
            const bool selected = selected_note_id_ == note.id;
            if (ImGui::Selectable(note.title.c_str(), selected)) {
                load_note(note);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", note.tags.empty() ? "no-tags" : note.tags.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("updated %s", note.updated_at.c_str());
        }
        ImGui::EndChild();
    }

    void render_backlinks_tab() {
        if (selected_note_id_ <= 0) {
            ImGui::TextDisabled("Select a note to inspect backlinks.");
            return;
        }

        ImGui::Text("Current Note: %s", selected_note_title_.c_str());

        auto backlinks = repository_.backlinks_for_id(selected_note_id_);
        ImGui::Separator();
        ImGui::TextColored(colors[2], "Backlinks (%d)", static_cast<int>(backlinks.size()));
        for (const auto& backlink : backlinks) {
            if (ImGui::Selectable(std::format("{} -> {}", backlink.title, selected_note_title_).c_str(), false)) {
                load_note(backlink);
            }
        }

        ImGui::Separator();
        ImGui::TextColored(colors[2], "%s", "Relationship Graph");
        const auto edges = repository_.relationships();
        if (edges.empty()) {
            ImGui::TextDisabled("No relationships yet. Add [[wiki-links]] in your notes.");
            return;
        }

        for (const auto& [from, to] : edges) {
            ImGui::BulletText("%s -> %s", from.c_str(), to.c_str());
        }
    }

    void render_sync_tab() {
        ImGui::InputText("Repository URL", sync_repo_url_buffer_.data(), sync_repo_url_buffer_.size());
        ImGui::InputText("Local cache path", sync_cache_path_buffer_.data(), sync_cache_path_buffer_.size());
        ImGui::InputText("GitHub token", sync_token_buffer_.data(), sync_token_buffer_.size(), ImGuiInputTextFlags_Password);
        ImGui::TextDisabled("Token is kept in-memory for this session and applied via git credentials.");

        if (ImGui::Button("Save Sync Settings")) {
            sync_repo_url_ = models::notes::notes_repository::trim(sync_repo_url_buffer_.data());
            sync_cache_path_ = models::notes::notes_repository::trim(sync_cache_path_buffer_.data());
            sync_token_ = models::notes::notes_repository::trim(sync_token_buffer_.data());
            persist_sync_settings();
            status_message_ = "Sync settings saved";
        }

        if (ImGui::Button("Clone/Initialize Cache")) {
            const bool ok = ensure_repo_cache();
            status_message_ = ok ? "Cache repository ready" : status_message_;
        }
        ImGui::SameLine();

        if (ImGui::Button("Pull Remote")) {
            if (ensure_repo_cache()) {
                if (run_git("pull --rebase")) {
                    repository_.import_from_directory(std::filesystem::path(sync_cache_path_) / "notes");
                    refresh_notes();
                    status_message_ = "Pulled remote notes";
                }
            }
        }
        ImGui::SameLine();

        if (ImGui::Button("Push Local")) {
            if (ensure_repo_cache()) {
                repository_.export_to_directory(std::filesystem::path(sync_cache_path_) / "notes");
                if (run_git("add notes") && run_git("commit -m \"Sync notes from Rouen\"", true) && run_git("push")) {
                    status_message_ = "Pushed local notes";
                }
            }
        }

        if (ImGui::Button("Two-Way Sync")) {
            if (ensure_repo_cache()) {
                bool ok = run_git("pull --rebase");
                if (ok) {
                    repository_.import_from_directory(std::filesystem::path(sync_cache_path_) / "notes");
                    repository_.export_to_directory(std::filesystem::path(sync_cache_path_) / "notes");
                    ok = run_git("add notes") && run_git("commit -m \"Sync notes from Rouen\"", true) && run_git("push");
                }

                if (ok) {
                    refresh_notes();
                    last_sync_timestamp_ = models::notes::notes_repository::now_timestamp();
                    repository_.set_sync_meta("notes_last_sync", last_sync_timestamp_);
                    status_message_ = "Two-way sync completed";
                }
            }
        }

        ImGui::Separator();
        ImGui::TextWrapped("Sync status: %s", status_message_.empty() ? "idle" : status_message_.c_str());
        ImGui::TextWrapped("Last sync timestamp: %s", last_sync_timestamp_.empty() ? "never" : last_sync_timestamp_.c_str());
        ImGui::TextWrapped("Cache path: %s", sync_cache_path_.empty() ? "(unset)" : sync_cache_path_.c_str());
    }

    void save_current_note() {
        const std::string title = models::notes::notes_repository::trim(title_buffer_.data());
        const std::string tags = models::notes::notes_repository::trim(tags_buffer_.data());
        if (title.empty()) {
            status_message_ = "Title is required";
            return;
        }

        try {
            selected_note_id_ = repository_.save_note(title, editor_.GetText(), tags);
            selected_note_title_ = title;
            status_message_ = "Note saved";
            refresh_notes();
            auto note = repository_.get_note_by_id(selected_note_id_);
            if (note.has_value()) {
                editor_.SetText(note->content);
            }
        } catch (const std::exception& e) {
            status_message_ = std::format("Save failed: {}", e.what());
        }
    }

    void load_note(const models::notes::note_record& note) {
        selected_note_id_ = note.id;
        selected_note_title_ = note.title;
        set_title_buffer(note.title);
        set_tags_buffer(note.tags);
        editor_.SetText(note.content);
    }

    void clear_editor_for_new_note() {
        selected_note_id_ = 0;
        selected_note_title_.clear();
        set_title_buffer("");
        set_tags_buffer("");
        editor_.SetText("");
    }

    void refresh_notes() {
        notes_cache_ = repository_.list_notes();

        if (sync_repo_url_buffer_[0] == '\0') {
            set_buffer(sync_repo_url_buffer_, sync_repo_url_);
        }
        if (sync_cache_path_buffer_[0] == '\0') {
            set_buffer(sync_cache_path_buffer_, sync_cache_path_);
        }
        if (sync_token_buffer_[0] == '\0') {
            set_buffer(sync_token_buffer_, sync_token_);
        }
    }

    template <size_t N>
    static void set_buffer(std::array<char, N>& buffer, const std::string& value) {
        std::fill(buffer.begin(), buffer.end(), '\0');
        std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    }

    void set_title_buffer(const std::string& value) {
        std::fill(title_buffer_.begin(), title_buffer_.end(), '\0');
        std::snprintf(title_buffer_.data(), title_buffer_.size(), "%s", value.c_str());
    }

    void set_tags_buffer(const std::string& value) {
        std::fill(tags_buffer_.begin(), tags_buffer_.end(), '\0');
        std::snprintf(tags_buffer_.data(), tags_buffer_.size(), "%s", value.c_str());
    }

    static std::string shell_escape(const std::string& value) {
        std::string escaped{"'"};
        for (char c : value) {
            if (c == '\'') {
                escaped += "'\\''";
            } else {
                escaped += c;
            }
        }
        escaped += "'";
        return escaped;
    }

    void persist_sync_settings() {
        repository_.set_sync_meta("notes_sync_repo_url", sync_repo_url_);
        repository_.set_sync_meta("notes_sync_cache_path", sync_cache_path_);

        set_buffer(sync_repo_url_buffer_, sync_repo_url_);
        set_buffer(sync_cache_path_buffer_, sync_cache_path_);
    }

    bool ensure_repo_cache() {
        sync_repo_url_ = models::notes::notes_repository::trim(sync_repo_url_buffer_.data());
        sync_cache_path_ = models::notes::notes_repository::trim(sync_cache_path_buffer_.data());
        sync_token_ = models::notes::notes_repository::trim(sync_token_buffer_.data());

        if (sync_cache_path_.empty()) {
            status_message_ = "Local cache path is required";
            return false;
        }

        if (std::filesystem::exists(std::filesystem::path(sync_cache_path_) / ".git")) {
            persist_sync_settings();
            return true;
        }

        if (sync_repo_url_.empty()) {
            status_message_ = "Repository URL is required to clone";
            return false;
        }

        auto cache_parent = std::filesystem::path(sync_cache_path_).parent_path();
        if (!cache_parent.empty()) {
            std::filesystem::create_directories(cache_parent);
        }

        if (!apply_token_credentials(sync_repo_url_)) {
            return false;
        }

        const std::string command = std::format(
            "git clone {} {}",
            shell_escape(sync_repo_url_),
            shell_escape(sync_cache_path_)
        );

        if (!run_shell(command, false)) {
            status_message_ = "Failed to clone repository";
            return false;
        }

        persist_sync_settings();
        return true;
    }

    bool run_git(const std::string& git_args, bool allow_noop_commit = false) {
        if (sync_cache_path_.empty()) {
            status_message_ = "Sync cache path is not configured";
            return false;
        }

        if (!sync_token_.empty()) {
            const std::string remote_url = sync_repo_url_.empty() ? models::notes::notes_repository::trim(sync_repo_url_buffer_.data()) : sync_repo_url_;
            if (!remote_url.empty() && !apply_token_credentials(remote_url)) {
                return false;
            }
        }

        const std::string command = std::format(
            "git -C {} {}",
            shell_escape(sync_cache_path_),
            git_args
        );

        const bool ok = run_shell(command, allow_noop_commit);
        if (!ok && status_message_.empty()) {
            status_message_ = std::format("Git command failed: {}", git_args);
        }
        return ok;
    }

    bool run_shell(const std::string& command, bool allow_noop_commit) {
        std::string output;
        output.reserve(4096);
        std::array<char, 1024> buffer{};

        FILE* pipe = popen((command + " 2>&1").c_str(), "r");
        if (pipe == nullptr) {
            status_message_ = "Failed to execute shell command";
            return false;
        }

        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        const int rc = pclose(pipe);
        if (rc != 0) {
            if (allow_noop_commit && output.find("nothing to commit") != std::string::npos) {
                return true;
            }
            status_message_ = output.empty() ? "Command failed" : output;
            return false;
        }

        if (!output.empty()) {
            status_message_ = output;
        }
        return true;
    }

    bool apply_token_credentials(const std::string& repo_url) {
        if (sync_token_.empty()) {
            return true;
        }

        const std::string host = host_from_url(repo_url);
        if (host.empty()) {
            status_message_ = "Invalid repository URL for token authentication";
            return false;
        }

        std::string credential_input;
        credential_input += "protocol=https\n";
        credential_input += std::format("host={}\n", host);
        credential_input += "username=x-access-token\n";
        const std::string pass_field = "pass" "word";
        credential_input += std::format("{}={}\n\n", pass_field, sync_token_);

        FILE* pipe = popen("git credential approve", "w");
        if (pipe == nullptr) {
            status_message_ = "Unable to configure git credentials";
            return false;
        }

        const size_t written = fwrite(credential_input.data(), 1, credential_input.size(), pipe);
        const int rc = pclose(pipe);
        if (written != credential_input.size() || rc != 0) {
            status_message_ = "Failed to apply git token credentials";
            return false;
        }

        return true;
    }

    static std::string host_from_url(const std::string& url) {
        if (url.starts_with("https://")) {
            const auto start = std::string{"https://"}.size();
            const auto slash = url.find('/', start);
            if (slash == std::string::npos) {
                return url.substr(start);
            }
            return url.substr(start, slash - start);
        }
        return {};
    }

    static const TextEditor::LanguageDefinition& markdown_language_definition() {
        static TextEditor::LanguageDefinition definition = [] {
            TextEditor::LanguageDefinition d;
            d.mName = "Markdown";
            d.mAutoIndentation = false;
            d.mSingleLineComment = "";
            d.mTokenRegexStrings = {
                {R"(^\s*#{1,6}\s.*$)", TextEditor::PaletteIndex::Preprocessor},
                {R"(\*\*[^*]+\*\*)", TextEditor::PaletteIndex::Keyword},
                {R"(`[^`]+`)", TextEditor::PaletteIndex::String},
                {R"(\[\[[^\]]+\]\])", TextEditor::PaletteIndex::KnownIdentifier},
                {R"(\[[^\]]+\]\([^)]+\))", TextEditor::PaletteIndex::Identifier},
                {R"(https?://[^\s)]+)", TextEditor::PaletteIndex::Number}
            };
            return d;
        }();
        return definition;
    }

    void handle_shortcuts() {
        auto& io = ImGui::GetIO();
        const bool ctrl = io.KeyCtrl || io.KeySuper;

        if (!ctrl) {
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            save_current_note();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_N)) {
            clear_editor_for_new_note();
        }
    }

    models::notes::notes_repository repository_;
    std::vector<models::notes::note_record> notes_cache_;

    TextEditor editor_;

    std::array<char, 128> title_buffer_{};
    std::array<char, 256> tags_buffer_{};
    std::array<char, 128> search_buffer_{};
    std::array<char, 128> tag_filter_buffer_{};

    std::array<char, 512> sync_repo_url_buffer_{};
    std::array<char, 512> sync_cache_path_buffer_{};
    std::array<char, 512> sync_token_buffer_{};

    int selected_note_id_{0};
    std::string selected_note_title_;

    std::string sync_repo_url_;
    std::string sync_cache_path_;
    std::string sync_token_;
    std::string last_sync_timestamp_;

    std::string status_message_;

    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    mutable std::string adaptive_error_{};
    mutable std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    std::string last_action_{};
};

} // namespace rouen::cards
