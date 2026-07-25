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
#include <SDL3/SDL.h>

#include "../interface/card.hpp"
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

    bool render(rouen::ui::ui_context& ui) override {
        return render_window([this, &ui]() {
            render_directory_content(ui);
        });
    }

private:
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
            contacts_ = repo_->get_all_contacts();
        } else {
            contacts_ = repo_->search_contacts(query);
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

        // Contacts List Container (no unnecessary border)
        ui.begin_child("contacts_list_child", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding);

        if (contacts_.empty()) {
            ui.spacing();
            ui.text_colored(colors[3], "No contacts found. Click 'New', 'macOS', or 'WhatsApp' to add contacts.");
        } else {
            for (const auto& c : contacts_) {
                render_contact_row(ui, c);
                ui.separator();
            }
        }

        ui.end_child();

        if (show_wa_modal_) {
            render_whatsapp_modal(ui);
        }
    }

    void render_contact_row(rouen::ui::ui_context& ui, const models::contacts::contact& c) {
        ImGui::PushID(static_cast<int>(c.id));

        // Render Avatar
        std::string avatar_url = c.get_avatar_url();
        bool has_tex = false;
        if (!avatar_url.empty() && image_cache_) {
            int tex_w = 0, tex_h = 0;
            SDL_Texture* tex = image_cache_->getTexture(renderer_, avatar_url, tex_w, tex_h);
            if (tex) {
                ImGui::Image(rouen::helpers::texture_id_cast(tex), ImVec2(42, 42));
                has_tex = true;
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
            repo_->delete_contact(c.id);
            helpers::UniversalSyncService::instance().sync_out("Deleted contact: " + c.get_full_name());
            reload_contacts();
        }

        ImGui::PopID();
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
