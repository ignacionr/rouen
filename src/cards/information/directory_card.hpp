#pragma once

#include <string>
#include <memory>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <format>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstring>
#include <iostream>
#include <SDL3/SDL.h>

#include "../interface/card.hpp"
#include "../../fonts.hpp"
#include "../../helpers/adaptive_cards/parser.hpp"
#include "../../helpers/adaptive_cards/renderer.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../models/contacts/contacts_repository.hpp"
#include "../../helpers/image_cache.hpp"
#include "../../helpers/texture_helper.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../helpers/universal_sync_service.hpp"
#include "../../registrar.hpp"
#include "../../../external/IconsMaterialDesign.h"

namespace rouen::cards {

struct directory_card : public card {
    explicit directory_card(std::string_view = "") {
        colors[0] = {0.18f, 0.52f, 0.92f, 1.0f}; // Primary blue
        colors[1] = {0.10f, 0.14f, 0.20f, 0.95f}; // Dark canvas
        colors[2] = {0.92f, 0.95f, 0.98f, 1.0f}; // Header
        colors[3] = {0.60f, 0.68f, 0.78f, 1.0f}; // Secondary text

        width = 620.0f;
        name("Contacts Directory");

        std::string db_path = rouen::platform::get_user_data_path("contacts.db").string();
        std::string img_db = rouen::platform::get_user_data_path("image_cache.db").string();
        std::string img_dir = rouen::platform::get_user_data_path("image_cache").string();

        repo_ = std::make_unique<models::contacts::contacts_repository>(db_path);
        image_cache_ = std::make_shared<::helpers::ImageCache>(img_db, img_dir, 30);

        reload_contacts();
    }

    void set_renderer(SDL_Renderer* r) {
        renderer_ = r;
    }

    std::string get_uri() const override {
        return "directory";
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
        icon_tb["text"] = "👥";
        icon_tb["size"] = "Large";
        icon_items.push_back(std::move(icon_tb));
        icon_col["items"] = std::move(icon_items);
        columns.push_back(std::move(icon_col));

        // Column 2: Title & Subtitle
        glz::json_t title_col;
        title_col["type"] = "Column";
        title_col["width"] = "stretch";
        std::vector<glz::json_t> title_items;

        glz::json_t title_tb;
        title_tb["type"] = "TextBlock";
        title_tb["text"] = "Contacts Directory";
        title_tb["weight"] = "Bolder";
        title_tb["size"] = "Large";
        title_items.push_back(std::move(title_tb));

        glz::json_t subtitle_tb;
        subtitle_tb["type"] = "TextBlock";
        subtitle_tb["text"] = "Enterprise and personal address book";
        subtitle_tb["isSubtle"] = true;
        subtitle_tb["size"] = "Small";
        title_items.push_back(std::move(subtitle_tb));

        title_col["items"] = std::move(title_items);
        columns.push_back(std::move(title_col));

        // Column 3: Count Badge
        glz::json_t count_col;
        count_col["type"] = "Column";
        count_col["width"] = "auto";
        std::vector<glz::json_t> count_items;
        glz::json_t count_tb;
        count_tb["type"] = "TextBlock";
        count_tb["text"] = std::format("{} Contacts", contacts_.size());
        count_tb["weight"] = "Bolder";
        count_tb["size"] = "Small";
        count_items.push_back(std::move(count_tb));
        count_col["items"] = std::move(count_items);
        columns.push_back(std::move(count_col));

        col_set["columns"] = std::move(columns);
        std::vector<glz::json_t> header_items;
        header_items.push_back(std::move(col_set));
        header["items"] = std::move(header_items);
        body.push_back(std::move(header));

        // 2. Search & Filter Container
        glz::json_t filter_container;
        filter_container["type"] = "Container";
        std::vector<glz::json_t> filter_items;

        glz::json_t search_input;
        search_input["type"] = "Input.Text";
        search_input["id"] = "search_term";
        search_input["placeholder"] = "Search by name, company, or email...";
        std::string cur_query = get_search_query();
        if (!cur_query.empty()) {
            search_input["value"] = cur_query;
        }
        filter_items.push_back(std::move(search_input));

        glz::json_t choice_set;
        choice_set["type"] = "Input.ChoiceSet";
        choice_set["id"] = "tag_filter";
        choice_set["label"] = "Filter by Group";
        choice_set["value"] = selected_tag_.empty() ? "All" : selected_tag_;

        std::vector<glz::json_t> choices;
        choices.push_back(glz::json_t::object_t{{"title", "All Contacts"}, {"value", "All"}});
        choices.push_back(glz::json_t::object_t{{"title", "Engineering Team"}, {"value", "Engineering"}});
        choices.push_back(glz::json_t::object_t{{"title", "Clients & Partners"}, {"value", "Clients"}});
        choices.push_back(glz::json_t::object_t{{"title", "VIP / Family"}, {"value", "VIP"}});
        choice_set["choices"] = std::move(choices);
        filter_items.push_back(std::move(choice_set));

        filter_container["items"] = std::move(filter_items);
        body.push_back(std::move(filter_container));

        // 3. FactSet (Contacts)
        glz::json_t fact_set;
        fact_set["type"] = "FactSet";
        std::vector<glz::json_t> facts;

        if (contacts_.empty()) {
            facts.push_back(glz::json_t::object_t{{"title", "Contacts:"}, {"value", "No contacts found"}});
        } else {
            for (const auto& c : contacts_) {
                std::string title = c.get_full_name() + ":";
                std::string detail;
                if (!c.email.empty()) detail += c.email;
                if (!c.phone.empty()) {
                    if (!detail.empty()) detail += " | ";
                    detail += c.phone;
                }
                if (!c.job_title.empty()) {
                    if (!detail.empty()) detail += " | ";
                    detail += c.job_title;
                } else if (!c.organization.empty()) {
                    if (!detail.empty()) detail += " | ";
                    detail += c.organization;
                }
                if (detail.empty()) detail = "No details available";
                facts.push_back(glz::json_t::object_t{{"title", std::move(title)}, {"value", std::move(detail)}});
            }
        }
        fact_set["facts"] = std::move(facts);
        body.push_back(std::move(fact_set));

        card["body"] = std::move(body);

        // 4. Actions
        std::vector<glz::json_t> actions;

        glz::json_t act_search;
        act_search["type"] = "Action.Execute";
        act_search["title"] = "Search Directory";
        act_search["verb"] = "search_contacts";
        actions.push_back(std::move(act_search));

        glz::json_t act_open;
        act_open["type"] = "Action.Execute";
        act_open["title"] = "Open Contact";
        act_open["verb"] = "open_contact";
        actions.push_back(std::move(act_open));

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

            if (verb == "search_contacts") {
                last_action_ = "search_contacts";
                std::string term;
                if (data.is_object()) {
                    if (data.contains("search_term") && data["search_term"].holds<std::string>()) {
                        term = data["search_term"].get<std::string>();
                    } else if (data.contains("search_query") && data["search_query"].holds<std::string>()) {
                        term = data["search_query"].get<std::string>();
                    } else if (data.contains("query") && data["query"].holds<std::string>()) {
                        term = data["query"].get<std::string>();
                    }
                    if (data.contains("tag_filter") && data["tag_filter"].holds<std::string>()) {
                        selected_tag_ = data["tag_filter"].get<std::string>();
                    } else if (data.contains("tag") && data["tag"].holds<std::string>()) {
                        selected_tag_ = data["tag"].get<std::string>();
                    }
                }
                search_query_ = term;
                std::memset(search_buf_, 0, sizeof(search_buf_));
                if (!term.empty()) {
                    std::strncpy(search_buf_, term.c_str(), sizeof(search_buf_) - 1);
                }
                reload_contacts();
            } else if (verb == "open_contact") {
                last_action_ = "open_contact";
                std::string cid_str;
                if (data.is_object()) {
                    if (data.contains("contact_id")) {
                        if (data["contact_id"].holds<std::string>()) {
                            cid_str = data["contact_id"].get<std::string>();
                        } else if (data["contact_id"].holds<double>()) {
                            cid_str = std::to_string(static_cast<int64_t>(data["contact_id"].get<double>()));
                        }
                    } else if (data.contains("id")) {
                        if (data["id"].holds<std::string>()) {
                            cid_str = data["id"].get<std::string>();
                        } else if (data["id"].holds<double>()) {
                            cid_str = std::to_string(static_cast<int64_t>(data["id"].get<double>()));
                        }
                    }
                }
                if (!cid_str.empty()) {
                    last_opened_contact_id_ = cid_str;
                    auto create_fn = registrar::try_get<std::function<void(const std::string&)>>("create_card");
                    if (create_fn && *create_fn) {
                        (*create_fn)("contact:" + cid_str);
                    }
                }
            } else if (verb == "add_contact") {
                last_action_ = "add_contact";
                models::contacts::contact new_c;
                if (data.is_object()) {
                    if (data.contains("name") && data["name"].holds<std::string>()) {
                        new_c.display_name = data["name"].get<std::string>();
                    }
                    if (data.contains("first_name") && data["first_name"].holds<std::string>()) {
                        new_c.first_name = data["first_name"].get<std::string>();
                    }
                    if (data.contains("last_name") && data["last_name"].holds<std::string>()) {
                        new_c.last_name = data["last_name"].get<std::string>();
                    }
                    if (data.contains("email") && data["email"].holds<std::string>()) {
                        new_c.email = data["email"].get<std::string>();
                    }
                    if (data.contains("phone") && data["phone"].holds<std::string>()) {
                        new_c.phone = data["phone"].get<std::string>();
                    }
                    if (data.contains("organization") && data["organization"].holds<std::string>()) {
                        new_c.organization = data["organization"].get<std::string>();
                    }
                    if (data.contains("notes") && data["notes"].holds<std::string>()) {
                        new_c.notes = data["notes"].get<std::string>();
                    }
                }
                if (new_c.display_name.empty() && !new_c.first_name.empty()) {
                    new_c.display_name = new_c.get_full_name();
                }
                if (!new_c.display_name.empty() || !new_c.email.empty() || !new_c.phone.empty()) {
                    int64_t new_id = repo_->upsert_contact(new_c);
                    last_added_contact_id_ = new_id;
                    helpers::UniversalSyncService::instance().sync_out("Added contact: " + new_c.get_full_name());
                    reload_contacts();
                }
            } else if (verb == "toggle_adaptive_view") {
                adaptive_view_mode = !adaptive_view_mode;
                last_action_ = "toggle_adaptive_view";
            }

            last_adaptive_parse_time_ = {};
        } catch (const std::exception& e) {
            std::cerr << "[directory] Action error: " << e.what() << std::endl;
        }
    }

    [[nodiscard]] bool is_adaptive_view_mode() const noexcept { return adaptive_view_mode; }
    void set_adaptive_view_mode(bool enabled) noexcept { adaptive_view_mode = enabled; }
    [[nodiscard]] std::string get_search_query() const {
        return search_buf_[0] != '\0' ? std::string(search_buf_) : search_query_;
    }
    [[nodiscard]] std::string get_search_term() const { return get_search_query(); }
    [[nodiscard]] const std::string& get_selected_tag() const noexcept { return selected_tag_; }
    [[nodiscard]] const std::string& get_tag_filter() const noexcept { return selected_tag_; }
    [[nodiscard]] size_t get_total_count() const noexcept { return contacts_.size(); }
    [[nodiscard]] const std::vector<models::contacts::contact>& get_contacts() const noexcept { return contacts_; }
    [[nodiscard]] const std::string& get_last_action() const noexcept { return last_action_; }
    [[nodiscard]] const std::string& get_last_opened_contact_id() const noexcept { return last_opened_contact_id_; }
    [[nodiscard]] int64_t get_last_added_contact_id() const noexcept { return last_added_contact_id_; }

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

            render_directory_content(ui);
        });
    }

private:
    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};

    std::string search_query_;
    std::string selected_tag_{"All"};
    std::string last_action_;
    std::string last_opened_contact_id_;
    int64_t last_added_contact_id_{-1};

    SDL_Renderer* renderer_{nullptr};
    std::unique_ptr<models::contacts::contacts_repository> repo_;
    std::shared_ptr<::helpers::ImageCache> image_cache_;
    std::vector<models::contacts::contact> contacts_;
    
    char search_buf_[128]{0};
    char wa_import_buf_[2048]{0};
    
    bool show_wa_modal_{false};
    std::string status_msg_;

    std::atomic<bool> is_importing_{false};
    std::atomic<float> import_progress_{0.0f};
    std::mutex import_status_mutex_;
    std::string import_status_text_;

    void reload_contacts() {
        std::string query = search_buf_;
        if (query.empty()) {
            query = search_query_;
        }
        if (query.empty()) {
            contacts_ = repo_->get_all_contacts();
        } else {
            contacts_ = repo_->search_contacts(query);
        }

        if (!selected_tag_.empty() && selected_tag_ != "All") {
            std::string tag_lower = selected_tag_;
            std::transform(tag_lower.begin(), tag_lower.end(), tag_lower.begin(), ::tolower);
            std::erase_if(contacts_, [&](const models::contacts::contact& c) {
                auto contains_ci = [&](std::string_view str) {
                    if (str.empty()) return false;
                    std::string s(str);
                    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                    return s.find(tag_lower) != std::string::npos;
                };
                return !(contains_ci(c.organization) || contains_ci(c.job_title) || contains_ci(c.notes));
            });
        }
    }

    void render_directory_content(rouen::ui::ui_context& ui) {
        // Top Toolbar
        ui.text_colored(colors[2], std::string(ICON_MD_CONTACTS) + " Contacts Directory");
        ui.same_line();
        ui.text_colored(colors[3], std::format("({} total)", contacts_.size()));

        ui.spacing();

        // Search Bar
        ui.push_item_width(240.0f);
        if (ui.input_text_with_placeholder("##search", search_buf_, sizeof(search_buf_), "Search contacts...")) {
            search_query_ = search_buf_;
            reload_contacts();
        }
        ui.pop_item_width();

        ui.same_line();
        if (ui.button((std::string(ICON_MD_PERSON_ADD) + " New").c_str())) {
            "create_card"_sfn("contact:new");
        }

        ui.same_line();
        if (ui.button((std::string(ICON_MD_IMPORT_EXPORT) + " macOS").c_str())) {
            if (!is_importing_.load()) {
                is_importing_.store(true);
                import_progress_.store(0.05f);
                {
                    std::lock_guard<std::mutex> lock(import_status_mutex_);
                    import_status_text_ = "Querying macOS Contacts application...";
                    status_msg_.clear();
                }

                std::thread([this]() {
                    int count = repo_->import_macos_contacts([this](float p, const std::string& status) {
                        import_progress_.store(p);
                        std::lock_guard<std::mutex> lock(import_status_mutex_);
                        import_status_text_ = status;
                    });

                    helpers::UniversalSyncService::instance().sync_out("Imported macOS contacts");

                    {
                        std::lock_guard<std::mutex> lock(import_status_mutex_);
                        status_msg_ = "Imported " + std::to_string(count) + " contacts from macOS Contacts";
                        import_status_text_.clear();
                    }
                    is_importing_.store(false);
                    reload_contacts();
                    "notify"_sfn("macOS contacts import complete: " + std::to_string(count) + " contacts");
                }).detach();
            }
        }

        ui.same_line();
        if (ui.button("WhatsApp")) {
            show_wa_modal_ = true;
            wa_import_buf_[0] = '\0';
        }

        ui.same_line();
        if (ui.button((std::string(ICON_MD_SYNC) + " Sync").c_str())) {
            bool ok = helpers::UniversalSyncService::instance().sync_in();
            reload_contacts();
            status_msg_ = ok ? "Universal sync completed" : "Sync failed";
        }

        // Live Progress Bar display for active non-blocking imports
        if (is_importing_.load()) {
            ui.spacing();
            float p = import_progress_.load();
            std::string st_text;
            {
                std::lock_guard<std::mutex> lock(import_status_mutex_);
                st_text = import_status_text_;
            }
            ImGui::ProgressBar(p, ImVec2(-1.0f, 18.0f), std::format("{:.0f}%", p * 100.0f).c_str());
            if (!st_text.empty()) {
                ui.text_colored(colors[3], st_text);
            }
            ui.spacing();
        } else if (!status_msg_.empty()) {
            ui.spacing();
            ui.text_colored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), status_msg_);
        }

        ui.spacing();
        ui.separator();
        ui.spacing();

        int64_t contact_to_delete = -1;
        std::string deleted_name;

        // Contacts List Container (no unnecessary border)
        ui.begin_child("contacts_list_child", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding);

        if (contacts_.empty()) {
            ui.spacing();
            ui.text_colored(colors[3], "No contacts found. Click 'New', 'macOS', or 'WhatsApp' to add contacts.");
        } else {
            for (const auto& c : contacts_) {
                if (render_contact_row(ui, c)) {
                    contact_to_delete = c.id;
                    deleted_name = c.get_full_name();
                }
                ui.separator();
            }
        }

        ui.end_child();

        if (contact_to_delete > 0) {
            repo_->delete_contact(contact_to_delete);
            helpers::UniversalSyncService::instance().sync_out("Deleted contact: " + deleted_name);
            reload_contacts();
        }

        if (show_wa_modal_) {
            render_whatsapp_modal(ui);
        }
    }

    bool render_contact_row(rouen::ui::ui_context& ui, const models::contacts::contact& c) {
        bool delete_clicked = false;
        ImGui::PushID(static_cast<int>(c.id));

        // Render Avatar
        std::string avatar_url = c.get_avatar_url();
        bool has_tex = false;
        if (!avatar_url.empty() && image_cache_) {
            int tex_w = 0, tex_h = 0;
            if (image_cache_->isCached(avatar_url, tex_w, tex_h)) {
                SDL_Texture* tex = image_cache_->getTexture(renderer_, avatar_url, tex_w, tex_h);
                if (tex) {
                    ImGui::Image(rouen::helpers::texture_id_cast(tex), ImVec2(42, 42));
                    has_tex = true;
                }
            } else {
                image_cache_->downloadAndCache(avatar_url);
            }
        }

        if (!has_tex) {
            std::string initials;
            if (!c.first_name.empty()) initials += c.first_name[0];
            if (!c.last_name.empty()) initials += c.last_name[0];
            if (initials.empty() && !c.display_name.empty()) initials += c.display_name[0];
            if (initials.empty()) initials = "?";
            std::transform(initials.begin(), initials.end(), initials.begin(), ::toupper);

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
            float radius = 21.0f;
            ImVec2 center(cursor_pos.x + radius, cursor_pos.y + radius);

            draw_list->AddCircleFilled(center, radius, IM_COL32(40, 100, 190, 220));
            ImVec2 tsize = ImGui::CalcTextSize(initials.c_str());
            draw_list->AddText(ImVec2(center.x - tsize.x * 0.5f, center.y - tsize.y * 0.5f),
                               IM_COL32(255, 255, 255, 255), initials.c_str());
            ImGui::Dummy(ImVec2(42, 42));
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        std::string name_str = c.get_full_name();
        ui.text_colored(colors[2], name_str);

        std::string info_line;
        if (!c.organization.empty()) info_line += c.organization;
        if (!c.email.empty()) {
            if (!info_line.empty()) info_line += " | ";
            info_line += c.email;
        } else if (!c.phone.empty()) {
            if (!info_line.empty()) info_line += " | ";
            info_line += c.phone;
        }
        if (!info_line.empty()) {
            ui.text_colored(colors[3], info_line);
        }
        ImGui::EndGroup();

        // Right side action buttons
        float avail_w = ImGui::GetContentRegionAvail().x;
        ui.same_line(avail_w - 110.0f);

        if (ui.button("Open")) {
            "create_card"_sfn("contact:" + std::to_string(c.id));
        }
        ui.same_line();
        if (ui.button("Del")) {
            delete_clicked = true;
        }

        ImGui::PopID();
        return delete_clicked;
    }


    void render_whatsapp_modal(rouen::ui::ui_context& ui) {
        ImGui::OpenPopup("Import WhatsApp Contacts");
        if (ImGui::BeginPopupModal("Import WhatsApp Contacts", &show_wa_modal_)) {
            ui.text_colored(colors[2], "Paste WhatsApp Contacts Data");
            ui.text_colored(colors[3], "Supports vCard (.vcf), JSON array, or 'Name: Phone' line exports");
            ui.spacing();

            ImGui::InputTextMultiline("##wadata", wa_import_buf_, sizeof(wa_import_buf_), ImVec2(420, 160));

            ui.spacing();
            ui.separator();
            ui.spacing();

            if (ui.button("Import Data")) {
                if (!is_importing_.load()) {
                    is_importing_.store(true);
                    import_progress_.store(0.05f);
                    {
                        std::lock_guard<std::mutex> lock(import_status_mutex_);
                        import_status_text_ = "Parsing and importing WhatsApp contacts...";
                        status_msg_.clear();
                    }
                    std::string data_copy = wa_import_buf_;
                    show_wa_modal_ = false;
                    ImGui::CloseCurrentPopup();

                    std::thread([this, data_copy]() {
                        int count = repo_->import_whatsapp_contacts(data_copy, [this](float p, const std::string& status) {
                            import_progress_.store(p);
                            std::lock_guard<std::mutex> lock(import_status_mutex_);
                            import_status_text_ = status;
                        });

                        helpers::UniversalSyncService::instance().sync_out("Imported WhatsApp contacts");

                        {
                            std::lock_guard<std::mutex> lock(import_status_mutex_);
                            status_msg_ = "Imported " + std::to_string(count) + " WhatsApp contacts";
                            import_status_text_.clear();
                        }
                        is_importing_.store(false);
                        reload_contacts();
                        "notify"_sfn("WhatsApp contacts import complete: " + std::to_string(count) + " contacts");
                    }).detach();
                }
            }
            ui.same_line();
            if (ui.button("Cancel")) {
                show_wa_modal_ = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
};

} // namespace rouen::cards
