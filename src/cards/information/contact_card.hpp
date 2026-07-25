#pragma once

#include <string>
#include <memory>
#include <vector>
#include <sstream>
#include <filesystem>
#include <format>
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

struct contact_card : public card {
    explicit contact_card(std::string_view locator) {
        // Card color palette
        colors[0] = {0.18f, 0.52f, 0.92f, 1.0f}; // Primary accent
        colors[1] = {0.12f, 0.16f, 0.22f, 0.95f}; // Background
        colors[2] = {0.92f, 0.95f, 0.98f, 1.0f}; // Header text
        colors[3] = {0.60f, 0.68f, 0.78f, 1.0f}; // Secondary text

        width = 440.0f;

        std::string db_path = rouen::platform::get_user_data_path("contacts.db").string();
        std::string img_db = rouen::platform::get_user_data_path("image_cache.db").string();
        std::string img_dir = rouen::platform::get_user_data_path("image_cache").string();
        
        repo_ = std::make_unique<models::contacts::contacts_repository>(db_path);
        image_cache_ = std::make_shared<::helpers::ImageCache>(img_db, img_dir, 30);

        std::string loc_str(locator);
        if (loc_str == "new" || loc_str.empty()) {
            contact_data_ = models::contacts::contact{};
            edit_mode_ = true;
            name("New Contact");
        } else {
            try {
                int64_t cid = std::stoll(loc_str);
                auto fetched = repo_->get_contact_by_id(cid);
                if (fetched.has_value()) {
                    contact_data_ = fetched.value();
                    name(contact_data_.get_full_name());
                } else {
                    contact_data_.id = cid;
                    edit_mode_ = true;
                    name("Contact #" + loc_str);
                }
            } catch (const std::exception&) {
                contact_data_ = models::contacts::contact{};
                edit_mode_ = true;
                name("New Contact");
            }
        }
        sync_buffers_from_data();
    }

    void set_renderer(SDL_Renderer* r) {
        renderer_ = r;
    }

    std::string get_uri() const override {
        if (contact_data_.id > 0) {
            return "contact:" + std::to_string(contact_data_.id);
        }
        return "contact:new";
    }

    bool render(rouen::ui::ui_context& ui) override {
        if (is_closed_) return false;
        return render_window([this, &ui]() {
            render_contact_content(ui);
        });
    }

private:
    SDL_Renderer* renderer_{nullptr};
    std::unique_ptr<models::contacts::contacts_repository> repo_;
    std::shared_ptr<::helpers::ImageCache> image_cache_;
    models::contacts::contact contact_data_;
    bool edit_mode_{false};
    bool show_delete_modal_{false};
    bool is_closed_{false};
    std::string status_msg_;

    // Edit buffers
    char buf_first_name_[128]{0};
    char buf_last_name_[128]{0};
    char buf_display_name_[128]{0};
    char buf_organization_[128]{0};
    char buf_job_title_[128]{0};
    char buf_email_[128]{0};
    char buf_phone_[128]{0};
    char buf_address_[256]{0};
    char buf_picture_url_[512]{0};
    char buf_notes_[1024]{0};

    void sync_buffers_from_data() {
        snprintf(buf_first_name_, sizeof(buf_first_name_), "%s", contact_data_.first_name.c_str());
        snprintf(buf_last_name_, sizeof(buf_last_name_), "%s", contact_data_.last_name.c_str());
        snprintf(buf_display_name_, sizeof(buf_display_name_), "%s", contact_data_.display_name.c_str());
        snprintf(buf_organization_, sizeof(buf_organization_), "%s", contact_data_.organization.c_str());
        snprintf(buf_job_title_, sizeof(buf_job_title_), "%s", contact_data_.job_title.c_str());
        snprintf(buf_email_, sizeof(buf_email_), "%s", contact_data_.email.c_str());
        snprintf(buf_phone_, sizeof(buf_phone_), "%s", contact_data_.phone.c_str());
        snprintf(buf_address_, sizeof(buf_address_), "%s", contact_data_.address.c_str());
        snprintf(buf_picture_url_, sizeof(buf_picture_url_), "%s", contact_data_.picture_url.c_str());
        snprintf(buf_notes_, sizeof(buf_notes_), "%s", contact_data_.notes.c_str());
    }

    void sync_data_from_buffers() {
        contact_data_.first_name = buf_first_name_;
        contact_data_.last_name = buf_last_name_;
        contact_data_.display_name = buf_display_name_;
        contact_data_.organization = buf_organization_;
        contact_data_.job_title = buf_job_title_;
        contact_data_.email = buf_email_;
        contact_data_.phone = buf_phone_;
        contact_data_.address = buf_address_;
        contact_data_.picture_url = buf_picture_url_;
        contact_data_.notes = buf_notes_;
    }

    void render_contact_content(rouen::ui::ui_context& ui) {
        // Header Bar with Gear Icon for Mode Toggle
        float avail_w = ImGui::GetContentRegionAvail().x;
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.3f, 0.45f, 0.4f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.52f, 0.92f, 0.6f));

        std::string mode_toggle_label = std::string(ICON_MD_SETTINGS) + (edit_mode_ ? " View Mode" : " Edit Mode");
        float mode_btn_w = ImGui::CalcTextSize(mode_toggle_label.c_str()).x + 16.0f;
        
        ui.same_line(avail_w - mode_btn_w);
        if (ui.button((mode_toggle_label + "##mode_toggle").c_str())) {
            edit_mode_ = !edit_mode_;
            if (!edit_mode_) {
                sync_buffers_from_data();
            }
        }
        ImGui::PopStyleColor(3);

        ui.spacing();

        if (edit_mode_) {
            render_edit_mode(ui);
        } else {
            render_view_mode(ui);
        }

        if (show_delete_modal_) {
            render_delete_modal(ui);
        }
    }

    void render_view_mode(rouen::ui::ui_context& ui) {
        // Avatar Photo & Identity Header
        std::string avatar_url = contact_data_.get_avatar_url();
        bool has_tex = false;

        if (!avatar_url.empty() && image_cache_) {
            int tex_w = 0, tex_h = 0;
            if (image_cache_->isCached(avatar_url, tex_w, tex_h)) {
                SDL_Texture* avatar_tex = image_cache_->getTexture(renderer_, avatar_url, tex_w, tex_h);
                if (avatar_tex) {
                    ImGui::Image(rouen::helpers::texture_id_cast(avatar_tex), ImVec2(72, 72));
                    has_tex = true;
                }
            } else {
                image_cache_->downloadAndCache(avatar_url);
            }
        }

        if (!has_tex) {
            // Render styled initials fallback avatar badge
            std::string initials;
            if (!contact_data_.first_name.empty()) initials += contact_data_.first_name[0];
            if (!contact_data_.last_name.empty()) initials += contact_data_.last_name[0];
            if (initials.empty() && !contact_data_.display_name.empty()) {
                initials += contact_data_.display_name[0];
            }
            if (initials.empty()) initials = "?";
            std::transform(initials.begin(), initials.end(), initials.begin(), ::toupper);

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
            float radius = 36.0f;
            ImVec2 center(cursor_pos.x + radius, cursor_pos.y + radius);

            draw_list->AddCircleFilled(center, radius, IM_COL32(46, 117, 219, 230));
            draw_list->AddCircle(center, radius, IM_COL32(100, 160, 240, 255), 32, 1.5f);

            ImVec2 text_size = ImGui::CalcTextSize(initials.c_str());
            draw_list->AddText(ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f),
                               IM_COL32(255, 255, 255, 255), initials.c_str());
            ImGui::Dummy(ImVec2(72, 72));
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        std::string full_name = contact_data_.get_full_name();
        ui.text_colored(colors[2], full_name);

        if (!contact_data_.job_title.empty() || !contact_data_.organization.empty()) {
            std::string subtitle;
            if (!contact_data_.job_title.empty()) subtitle += contact_data_.job_title;
            if (!contact_data_.job_title.empty() && !contact_data_.organization.empty()) subtitle += " at ";
            if (!contact_data_.organization.empty()) subtitle += contact_data_.organization;
            ui.text_colored(colors[3], subtitle);
        }

        // Source badge
        std::string src_badge = "Source: " + contact_data_.source;
        if (contact_data_.source == "macos") src_badge += " (macOS Contacts)";
        else if (contact_data_.source == "whatsapp") src_badge += " (WhatsApp)";
        ui.text_colored(ImVec4(0.35f, 0.75f, 0.55f, 0.9f), src_badge);
        ImGui::EndGroup();

        ui.spacing();
        ui.separator();
        ui.spacing();

        // Contact Details Section
        auto email_list = contact_data_.get_email_list();
        if (!email_list.empty()) {
            ui.text_colored(colors[3], std::string(ICON_MD_EMAIL) + " Email(s)");
            for (size_t i = 0; i < email_list.size(); ++i) {
                const auto& em = email_list[i];
                ui.text_colored(colors[2], em);
                ui.same_line();
                if (ui.button((std::string(ICON_MD_CONTENT_COPY) + " Copy##email_" + std::to_string(i)).c_str())) {
                    SDL_SetClipboardText(em.c_str());
                    status_msg_ = "Email copied: " + em;
                }
            }
        }

        auto phone_list = contact_data_.get_phone_list();
        if (!phone_list.empty()) {
            ui.spacing();
            ui.text_colored(colors[3], std::string(ICON_MD_PHONE) + " Phone(s)");
            for (size_t i = 0; i < phone_list.size(); ++i) {
                const auto& ph = phone_list[i];
                ui.text_colored(colors[2], ph);
                ui.same_line();
                if (ui.button((std::string(ICON_MD_CONTENT_COPY) + " Copy##phone_" + std::to_string(i)).c_str())) {
                    SDL_SetClipboardText(ph.c_str());
                    status_msg_ = "Phone copied: " + ph;
                }
                ui.same_line();
                if (ui.button(("WhatsApp##wa_" + std::to_string(i)).c_str())) {
                    std::string wa_url = "https://wa.me/" + clean_phone_number(ph);
                    rouen::platform::open_url(wa_url);
                }
            }
        }

        if (!contact_data_.address.empty()) {
            ui.spacing();
            ui.text_colored(colors[3], std::string(ICON_MD_LOCATION_ON) + " Address");
            ui.text_colored(colors[2], contact_data_.address);
        }

        if (!contact_data_.notes.empty()) {
            ui.spacing();
            ui.text_colored(colors[3], std::string(ICON_MD_NOTE) + " Notes");
            ui.text_colored(colors[2], contact_data_.notes);
        }

        ui.spacing();
        ui.separator();
        ui.spacing();

        // Actions Toolbar
        if (ui.button((std::string(ICON_MD_EDIT) + " Edit Contact").c_str())) {
            edit_mode_ = true;
            sync_buffers_from_data();
        }
        ui.same_line();
        if (ui.button((std::string(ICON_MD_DELETE) + " Delete Contact").c_str())) {
            show_delete_modal_ = true;
        }

        if (!status_msg_.empty()) {
            ui.spacing();
            ui.text_colored(ImVec4(0.4f, 0.85f, 0.5f, 1.0f), status_msg_);
        }
    }

    void render_edit_mode(rouen::ui::ui_context& ui) {
        ui.text_colored(colors[2], contact_data_.id > 0 ? "Edit Contact Details" : "Create New Contact");
        ui.spacing();

        ui.input_text_with_placeholder("First Name", buf_first_name_, sizeof(buf_first_name_), "e.g. John");
        ui.input_text_with_placeholder("Last Name", buf_last_name_, sizeof(buf_last_name_), "e.g. Doe");
        ui.input_text_with_placeholder("Display Name", buf_display_name_, sizeof(buf_display_name_), "e.g. John Doe");
        ui.input_text_with_placeholder("Organization", buf_organization_, sizeof(buf_organization_), "e.g. Acme Corp");
        ui.input_text_with_placeholder("Job Title", buf_job_title_, sizeof(buf_job_title_), "e.g. Lead Engineer");
        ui.input_text_with_placeholder("Email", buf_email_, sizeof(buf_email_), "e.g. john@example.com");
        ui.input_text_with_placeholder("Phone", buf_phone_, sizeof(buf_phone_), "e.g. +1 555-0199");
        ui.input_text_with_placeholder("Address", buf_address_, sizeof(buf_address_), "e.g. 123 Main St, NY");
        ui.input_text_with_placeholder("Picture URL (or Gravatar auto-fallback)", buf_picture_url_, sizeof(buf_picture_url_), "https://example.com/avatar.jpg");
        
        ui.spacing();
        ui.text_colored(colors[3], "Notes");
        ImGui::InputTextMultiline("##notes", buf_notes_, sizeof(buf_notes_), ImVec2(-1, 80));

        ui.spacing();
        ui.separator();
        ui.spacing();

        if (ui.button((std::string(ICON_MD_SAVE) + " Save Contact").c_str())) {
            sync_data_from_buffers();
            repo_->upsert_contact(contact_data_);
            
            // Trigger universal sync update
            helpers::UniversalSyncService::instance().sync_out("Updated contact: " + contact_data_.get_full_name());
            
            // Notify system
            "notify"_sfn("Contact saved: " + contact_data_.get_full_name());
            
            edit_mode_ = false;
            name(contact_data_.get_full_name());
            status_msg_ = "Contact saved successfully";
        }
        ui.same_line();
        if (ui.button("Cancel")) {
            edit_mode_ = false;
            sync_buffers_from_data();
        }
    }

    void render_delete_modal(rouen::ui::ui_context& ui) {
        ImGui::OpenPopup("Delete Contact Confirmation");
        if (ImGui::BeginPopupModal("Delete Contact Confirmation", &show_delete_modal_)) {
            ui.text_colored(colors[2], "Are you sure you want to delete this contact?");
            ui.text_colored(colors[3], contact_data_.get_full_name());
            ui.spacing();
            ui.separator();
            ui.spacing();

            if (ui.button("Yes, Delete")) {
                if (contact_data_.id > 0) {
                    repo_->delete_contact(contact_data_.id);
                    helpers::UniversalSyncService::instance().sync_out("Deleted contact: " + contact_data_.get_full_name());
                    "notify"_sfn("Contact deleted: " + contact_data_.get_full_name());
                }
                show_delete_modal_ = false;
                edit_mode_ = false;
                is_closed_ = true;
            }
            ui.same_line();
            if (ui.button("Cancel")) {
                show_delete_modal_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    static std::string clean_phone_number(const std::string& raw) {
        std::string clean;
        for (char ch : raw) {
            if (std::isdigit(ch)) clean += ch;
        }
        return clean;
    }
};

} // namespace rouen::cards
