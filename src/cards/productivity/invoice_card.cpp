#include "invoice_card.hpp"
#include "cards/interface/card.hpp"
#include "models/contacts/contact.hpp"
#include "models/contacts/contacts_repository.hpp"

#include <EPDFVersion.h>
#include <EStatusCode.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <memory>
#include <vector>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#pragma clang diagnostic ignored "-Wdocumentation-unknown-command"
#pragma clang diagnostic ignored "-Wnewline-eof"
#endif

#include <PDFPage.h>
#include <PDFRectangle.h>
#include <PDFUsedFont.h>
#include <PDFWriter.h>
#include <PageContentContext.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "../../helpers/platform_utils.hpp"
#include "../../helpers/ui_context.hpp"
#include "../../registrar.hpp"
#include "../../../external/IconsMaterialDesign.h"

namespace rouen::cards {

static const char* k_months[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

invoice_card::invoice_card() {
    colors[0] = {0.18f, 0.52f, 0.88f, 1.0f}; // Sapphire blue accent
    colors[1] = {0.12f, 0.75f, 0.65f, 0.8f}; // Teal accent
    name("Invoice Generator");
    width = 640.0f;

    const char* home = std::getenv("HOME");
    if (home) {
        pdf_output_path = (std::filesystem::path(home) / "Desktop" / "Invoice_INV-2026-001.pdf").string();
    } else {
        pdf_output_path = "Invoice_INV-2026-001.pdf";
    }

    std::string const db_path = rouen::platform::get_user_data_path("contacts.db").string();
    contacts_repo_ = std::make_unique<rouen::models::contacts::contacts_repository>(db_path);
    refresh_contacts();
}

void invoice_card::refresh_contacts() {
    if (contacts_repo_) {
        cached_contacts_ = contacts_repo_->get_all_contacts();
        contacts_loaded_ = true;
    }
}

void invoice_card::apply_contact_as_provider(const rouen::models::contacts::contact& c) {
    seller_name = c.get_full_name();
    seller_email = c.email;
    seller_phone = c.phone;
    seller_address = c.address;
    if (!seller_name.empty()) {
        account_holder = seller_name;
    }
    status_message = "Provider updated from contact: " + seller_name;
    status_is_error = false;
}

void invoice_card::apply_contact_as_customer(const rouen::models::contacts::contact& c) {
    if (!c.organization.empty()) {
        client_name = c.organization;
        std::string const full_n = c.get_full_name();
        if (!full_n.empty() && full_n != "Unnamed Contact") {
            client_contact = full_n;
            if (!c.email.empty()) {
                client_contact += " (" + c.email + ")";
            }
        } else {
            client_contact = c.email;
        }
    } else {
        client_name = c.get_full_name();
        client_contact = c.email;
    }
    client_address = c.address;
    status_message = "Customer updated from contact: " + client_name;
    status_is_error = false;
}

std::string invoice_card::get_swift_bic_label() const {
    if (swift_bic_type == 0) return "SWIFT Code";
    if (swift_bic_type == 1) return "BIC Code";
    return "SWIFT / BIC Code";
}

void invoice_card::apply_monthly_retainer() {
    int m = retainer_month_idx;
    if (m < 0 || m > 11) m = 6;
    std::string month_year = std::format("{} {}", k_months[m], retainer_year);
    items.clear();
    items.push_back({
        .description = std::format("Monthly Consulting Retainer - {}", month_year),
        .quantity = 1.0,
        .unit_price = retainer_amount
    });
}

double invoice_card::calculate_subtotal() const {
    double total = 0.0;
    for (const auto& item : items) {
        total += item.amount();
    }
    return total;
}

double invoice_card::calculate_total() const {
    return calculate_subtotal();
}

bool invoice_card::generate_pdf(const std::string& output_path, std::string& error_msg) const {
    try {
        std::filesystem::path const p(output_path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }

        PDFWriter pdfWriter;
        EStatusCode status = pdfWriter.StartPDF(output_path, ePDFVersion14);
        if (status != eSuccess) {
            error_msg = "PDFHummus failed to create PDF at path: " + output_path;
            return false;
        }

        // Standard A4 Page (595 x 842 points)
        PDFPage* page = new PDFPage();
        page->SetMediaBox(PDFRectangle(0, 0, 595, 842));
        PageContentContext* ctx = pdfWriter.StartPageContentContext(page);
        if (!ctx) {
            error_msg = "Failed to start page content context in PDFHummus.";
            return false;
        }

        // Search for standard TTF font
        std::vector<std::string> const font_candidates = {
            "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/System/Library/Fonts/Helvetica.ttf",
            "/Library/Fonts/Arial.ttf",
            "external/fonts/NotoSansSymbols-Regular.ttf",
            "external/PDFWriter/PDFWriterTesting/Materials/fonts/arial.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
        };

        PDFUsedFont* font = nullptr;
        for (const auto& candidate : font_candidates) {
            if (std::filesystem::exists(candidate)) {
                font = pdfWriter.GetFontForFile(candidate);
                if (font) break;
            }
        }

        auto write_text = [&](const std::string& text, double x, double y, double size, double r = 0.0, double g = 0.0, double b = 0.0) {
            ctx->BT();
            if (font) {
                ctx->Tf(font, size);
            }
            ctx->rg(r, g, b);
            ctx->Tm(1, 0, 0, 1, x, y);
            ctx->Tj(text);
            ctx->ET();
        };

        auto write_text_right = [&](const std::string& text, double right_x, double y, double size, double r = 0.0, double g = 0.0, double b = 0.0) {
            double const approx_width = static_cast<double>(text.length()) * size * 0.52;
            write_text(text, right_x - approx_width, y, size, r, g, b);
        };

        auto draw_rect = [&](double x, double y, double w, double h, double r, double g, double b, bool fill = true) {
            ctx->q();
            if (fill) {
                ctx->rg(r, g, b);
                ctx->re(x, y, w, h);
                ctx->f();
            } else {
                ctx->RG(r, g, b);
                ctx->w(1);
                ctx->re(x, y, w, h);
                ctx->s();
            }
            ctx->Q();
        };

        auto draw_line = [&](double x1, double y1, double x2, double y2, double r, double g, double b, double line_width = 1.0) {
            ctx->q();
            ctx->RG(r, g, b);
            ctx->w(line_width);
            ctx->m(x1, y1);
            ctx->l(x2, y2);
            ctx->s();
            ctx->Q();
        };

        // Render Invoice Content
        double y = 800.0;

        // Top Header Banner
        draw_rect(40, 785, 515, 35, 0.15, 0.35, 0.65, true);
        write_text("INVOICE", 50, 796, 20, 1.0, 1.0, 1.0);
        write_text_right("# " + invoice_number, 545, 796, 14, 1.0, 1.0, 1.0);

        y = 760.0;

        // Seller Information
        write_text(seller_name, 40, y, 14, 0.1, 0.1, 0.1); y -= 16.0;
        if (!seller_email.empty()) { write_text(seller_email, 40, y, 10, 0.4, 0.4, 0.4); y -= 14.0; }
        if (!seller_phone.empty()) { write_text(seller_phone, 40, y, 10, 0.4, 0.4, 0.4); y -= 14.0; }
        if (!seller_address.empty()) { write_text(seller_address, 40, y, 10, 0.4, 0.4, 0.4); y -= 14.0; }

        // Meta Information
        double meta_y = 760.0;
        write_text_right("Invoice Date: " + invoice_date, 545, meta_y, 10, 0.2, 0.2, 0.2); meta_y -= 14.0;
        write_text_right("Payment Terms: " + payment_terms, 545, meta_y, 10, 0.2, 0.2, 0.2); meta_y -= 14.0;
        write_text_right("Currency: " + currency, 545, meta_y, 10, 0.2, 0.2, 0.2); meta_y -= 14.0;

        y = std::min(y, meta_y) - 15.0;

        // Separator
        draw_line(40, y, 555, y, 0.8, 0.8, 0.8, 1.0);
        y -= 20.0;

        // Client / Billed To Section
        write_text("BILLED TO:", 40, y, 11, 0.15, 0.35, 0.65); y -= 16.0;
        write_text(client_name, 40, y, 12, 0.1, 0.1, 0.1); y -= 15.0;
        if (!client_contact.empty()) { write_text(client_contact, 40, y, 10, 0.4, 0.4, 0.4); y -= 14.0; }
        if (!client_address.empty()) { write_text(client_address, 40, y, 10, 0.4, 0.4, 0.4); y -= 14.0; }

        y -= 15.0;

        // Line Items Table Header
        draw_rect(40, y - 5, 515, 22, 0.92, 0.94, 0.96, true);
        write_text("Description of Services", 50, y, 10, 0.2, 0.2, 0.2);
        write_text_right("Qty / Hrs", 380, y, 10, 0.2, 0.2, 0.2);
        write_text_right("Unit Rate", 460, y, 10, 0.2, 0.2, 0.2);
        write_text_right("Amount", 545, y, 10, 0.2, 0.2, 0.2);
        y -= 22.0;

        // Table Rows
        bool alt_row = false;
        for (const auto& item : items) {
            if (alt_row) {
                draw_rect(40, y - 4, 515, 20, 0.97, 0.98, 0.99, true);
            }
            write_text(item.description, 50, y, 10, 0.1, 0.1, 0.1);
            write_text_right(std::format("{:.2f}", item.quantity), 380, y, 10, 0.2, 0.2, 0.2);
            write_text_right(std::format("{:.2f}", item.unit_price), 460, y, 10, 0.2, 0.2, 0.2);
            write_text_right(std::format("{:.2f} {}", item.amount(), currency), 545, y, 10, 0.1, 0.1, 0.1);

            draw_line(40, y - 5, 555, y - 5, 0.9, 0.9, 0.9, 0.5);
            y -= 20.0;
            alt_row = !alt_row;
        }

        y -= 10.0;

        // Subtotal & Total
        double total_due = calculate_total();
        draw_rect(340, y - 25, 215, 30, 0.15, 0.35, 0.65, true);
        write_text("TOTAL AMOUNT DUE:", 350, y - 14, 11, 1.0, 1.0, 1.0);
        write_text_right(std::format("{:.2f} {}", total_due, currency), 545, y - 14, 12, 1.0, 1.0, 1.0);

        y -= 55.0;

        // International Payment Info Box
        draw_rect(40, y - 85, 515, 90, 0.96, 0.97, 0.98, true);
        draw_rect(40, y - 85, 515, 90, 0.85, 0.88, 0.92, false);

        double bank_y = y - 12.0;
        std::string const swift_bic_label = get_swift_bic_label();
        write_text("INTERNATIONAL PAYMENT DETAILS (" + swift_bic_label + " / IBAN)", 50, bank_y, 10, 0.15, 0.35, 0.65); bank_y -= 15.0;
        write_text("Bank Name: " + bank_name, 50, bank_y, 9, 0.2, 0.2, 0.2);
        write_text(swift_bic_label + ": " + swift_code, 300, bank_y, 9, 0.2, 0.2, 0.2); bank_y -= 14.0;
        write_text("Account Holder: " + account_holder, 50, bank_y, 9, 0.2, 0.2, 0.2);
        write_text("IBAN / Account: " + iban_account, 300, bank_y, 9, 0.2, 0.2, 0.2); bank_y -= 14.0;

        y -= 105.0;

        // Statement of Foreign Performance
        draw_rect(40, y - 28, 515, 30, 0.98, 0.95, 0.92, true);
        draw_rect(40, y - 28, 515, 30, 0.9, 0.8, 0.7, false);
        write_text("Foreign Performance Statement:", 50, y - 12, 9, 0.6, 0.3, 0.1);
        write_text(foreign_statement, 50, y - 23, 9, 0.2, 0.2, 0.2);

        // Footer Text
        write_text("Thank you for your business!", 230, 40, 10, 0.5, 0.5, 0.5);

        pdfWriter.EndPageContentContext(ctx);
        pdfWriter.WritePageAndRelease(page);
        status = pdfWriter.EndPDF();

        if (status != eSuccess) {
            error_msg = "PDFHummus failed to finalize PDF output file.";
            return false;
        }

        return true;
    } catch (const std::exception& ex) {
        error_msg = std::string("Exception generating PDF: ") + ex.what();
        return false;
    }
}

void invoice_card::render_contact_picker_modal(rouen::ui::ui_context& ui) {
    if (!show_contact_picker_) return;

    std::string const modal_title = (picker_target_ == contact_picker_target::provider)
        ? "Select Provider from Contacts"
        : "Select Customer from Contacts";

    ImGui::OpenPopup(modal_title.c_str());
    ImGui::SetNextWindowSize(ImVec2(540, 400), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(modal_title.c_str(), &show_contact_picker_)) {
        ui.text_colored(colors[0], picker_target_ == contact_picker_target::provider
            ? "Select a contact to fill Provider (Seller) information:"
            : "Select a contact to fill Customer (Client) information:");

        ui.spacing();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##contact_search", "Search contacts by name, company, email, phone...", contact_search_buf_, sizeof(contact_search_buf_));

        ui.spacing();

        std::string filter_str = contact_search_buf_;
        std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

        if (ImGui::BeginChild("ContactListChild", ImVec2(0, 240), true)) {
            if (cached_contacts_.empty()) {
                ui.text_colored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No contacts found in contacts database.");
            } else {
                int match_count = 0;
                for (const auto& c : cached_contacts_) {
                    std::string const full_name = c.get_full_name();
                    std::string search_target = full_name + " " + c.organization + " " + c.email + " " + c.phone;
                    std::transform(search_target.begin(), search_target.end(), search_target.begin(), ::tolower);

                    if (!filter_str.empty() && search_target.find(filter_str) == std::string::npos) {
                        continue;
                    }

                    match_count++;
                    ImGui::PushID(static_cast<int>(c.id));

                    std::string display_title = std::string(ICON_MD_CONTACTS) + "  " + full_name;
                    if (!c.organization.empty()) {
                        display_title += " (" + c.organization + ")";
                    }
                    if (!c.job_title.empty()) {
                        display_title += " - " + c.job_title;
                    }

                    if (ImGui::Selectable(display_title.c_str(), false)) {
                        if (picker_target_ == contact_picker_target::provider) {
                            apply_contact_as_provider(c);
                        } else {
                            apply_contact_as_customer(c);
                        }
                        show_contact_picker_ = false;
                        ImGui::CloseCurrentPopup();
                    }

                    if (!c.email.empty() || !c.phone.empty() || !c.address.empty()) {
                        std::string subtitle;
                        if (!c.email.empty()) subtitle += c.email;
                        if (!c.phone.empty()) {
                            if (!subtitle.empty()) subtitle += " | ";
                            subtitle += c.phone;
                        }
                        if (!c.address.empty()) {
                            if (!subtitle.empty()) subtitle += " | ";
                            subtitle += c.address;
                        }
                        ui.text_colored(ImVec4(0.55f, 0.65f, 0.75f, 1.0f), "     " + subtitle);
                    }

                    ui.separator();
                    ImGui::PopID();
                }

                if (match_count == 0) {
                    ui.text_colored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No contacts match search filter.");
                }
            }
        }
        ImGui::EndChild();

        ui.spacing();
        if (ui.button("Cancel", ImVec2(100, 0))) {
            show_contact_picker_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

bool invoice_card::render(rouen::ui::ui_context& ui) {
    return render_window([this, &ui]() {
        ui.text_colored(colors[0], "Professional Invoice Generator (PDFHummus)");
        ui.text_wrapped("Fill in invoice details, load provider/customer from Contacts, or use the Monthly Retainer preset.");
        
        ui.spacing();
        if (ImGui::Button((std::string(ICON_MD_CONTACTS) + " Choose Provider from Contacts##top_provider_btn").c_str())) {
            refresh_contacts();
            picker_target_ = contact_picker_target::provider;
            show_contact_picker_ = true;
            contact_search_buf_[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ICON_MD_CONTACTS) + " Choose Customer from Contacts##top_customer_btn").c_str())) {
            refresh_contacts();
            picker_target_ = contact_picker_target::customer;
            show_contact_picker_ = true;
            contact_search_buf_[0] = '\0';
        }

        ui.separator();

        auto draw_input_string = [](const char* label, std::string& str) {
            char buf[512];
            std::strncpy(buf, str.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText(label, buf, sizeof(buf))) {
                str = buf;
                return true;
            }
            return false;
        };

        if (ImGui::BeginTabBar("InvoiceTabs")) {
            // Tab 1: Provider, Client, & Metadata
            if (ImGui::BeginTabItem("Parties & Meta")) {
                ui.text_colored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Provider (Seller / Freelancer)");

                if (ImGui::Button((std::string(ICON_MD_CONTACTS) + " Choose Provider from Contacts...##provider_btn").c_str())) {
                    refresh_contacts();
                    picker_target_ = contact_picker_target::provider;
                    show_contact_picker_ = true;
                    contact_search_buf_[0] = '\0';
                }

                if (!cached_contacts_.empty()) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(210);
                    std::string const combo_preview = (selected_provider_combo_idx_ >= 0 && selected_provider_combo_idx_ < static_cast<int>(cached_contacts_.size()))
                        ? cached_contacts_[static_cast<size_t>(selected_provider_combo_idx_)].get_full_name()
                        : "Quick Select Contact...";
                    if (ImGui::BeginCombo("##provider_combo", combo_preview.c_str())) {
                        for (size_t i = 0; i < cached_contacts_.size(); ++i) {
                            const bool is_selected = (selected_provider_combo_idx_ == static_cast<int>(i));
                            std::string item_label = cached_contacts_[i].get_full_name();
                            if (!cached_contacts_[i].organization.empty()) {
                                item_label += " (" + cached_contacts_[i].organization + ")";
                            }
                            if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                                selected_provider_combo_idx_ = static_cast<int>(i);
                                apply_contact_as_provider(cached_contacts_[i]);
                            }
                            if (is_selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                draw_input_string("Full Name##seller", seller_name);
                draw_input_string("Email / Contact##seller", seller_email);
                draw_input_string("Address / Country##seller", seller_address);
                draw_input_string("Phone Number##seller", seller_phone);

                ui.spacing();
                ui.separator();
                ui.text_colored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Client (Customer / Company)");

                if (ImGui::Button((std::string(ICON_MD_CONTACTS) + " Choose Customer from Contacts...##client_btn").c_str())) {
                    refresh_contacts();
                    picker_target_ = contact_picker_target::customer;
                    show_contact_picker_ = true;
                    contact_search_buf_[0] = '\0';
                }

                if (!cached_contacts_.empty()) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(210);
                    std::string const combo_preview = (selected_customer_combo_idx_ >= 0 && selected_customer_combo_idx_ < static_cast<int>(cached_contacts_.size()))
                        ? cached_contacts_[static_cast<size_t>(selected_customer_combo_idx_)].get_full_name()
                        : "Quick Select Contact...";
                    if (ImGui::BeginCombo("##customer_combo", combo_preview.c_str())) {
                        for (size_t i = 0; i < cached_contacts_.size(); ++i) {
                            const bool is_selected = (selected_customer_combo_idx_ == static_cast<int>(i));
                            std::string item_label = cached_contacts_[i].get_full_name();
                            if (!cached_contacts_[i].organization.empty()) {
                                item_label += " (" + cached_contacts_[i].organization + ")";
                            }
                            if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                                selected_customer_combo_idx_ = static_cast<int>(i);
                                apply_contact_as_customer(cached_contacts_[i]);
                            }
                            if (is_selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                draw_input_string("Company / Name##client", client_name);
                draw_input_string("Billing Contact##client", client_contact);
                draw_input_string("Company Address##client", client_address);

                ui.spacing();
                ui.separator();
                ui.text_colored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Invoice Metadata");
                draw_input_string("Invoice Number##meta", invoice_number);
                draw_input_string("Invoice Date##meta", invoice_date);
                draw_input_string("Payment Terms##meta", payment_terms);
                draw_input_string("Currency##meta", currency);

                ImGui::EndTabItem();
            }

            // Tab 2: Services / Line Items
            if (ImGui::BeginTabItem("Services & Line Items")) {
                // Fixed Month Billing Retainer Section
                ui.text_colored(colors[1], "Monthly Retainer Contract Quick Setup:");
                
                ImGui::SetNextItemWidth(130);
                ImGui::Combo("Month##retainer", &retainer_month_idx, k_months, 12);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90);
                ImGui::InputInt("Year##retainer", &retainer_year, 1, 1);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120);
                ImGui::InputDouble("Rate ($)##retainer", &retainer_amount, 500.0, 1000.0, "%.2f");

                if (ImGui::Button("Set Monthly Retainer Preset ($10,000.00)", ImVec2(280, 26))) {
                    apply_monthly_retainer();
                }
                ImGui::SameLine();
                ui.text_colored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Sets Qty=1, Rate=$10,000.00, Description='Monthly Consulting Retainer - [Month Year]'");

                ui.separator();
                ui.spacing();

                ui.text_colored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Line Items (Services Provided)");

                for (size_t i = 0; i < items.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    ui.text_colored(colors[1], std::format("Item #{}", i + 1));
                    ImGui::SameLine();
                    if (ImGui::Button("Remove")) {
                        items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
                        ImGui::PopID();
                        break;
                    }

                    draw_input_string("Description", items[i].description);
                    ImGui::SetNextItemWidth(120);
                    ImGui::InputDouble("Qty / Hours", &items[i].quantity, 1.0, 5.0, "%.2f");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(120);
                    ImGui::InputDouble("Unit Rate", &items[i].unit_price, 5.0, 25.0, "%.2f");
                    ImGui::SameLine();
                    ui.text_colored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), std::format("Amount: {:.2f} {}", items[i].amount(), currency));

                    ui.separator();
                    ImGui::PopID();
                }

                if (ImGui::Button("+ Add Custom Line Item")) {
                    items.push_back({"Software Development Work", 1.0, 100.0});
                }

                ui.spacing();
                ui.text_colored(colors[0], std::format("Total Amount Due: {:.2f} {}", calculate_total(), currency));

                ImGui::EndTabItem();
            }

            // Tab 3: Banking & Declarations
            if (ImGui::BeginTabItem("Payment & Legal")) {
                ui.text_colored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "International Banking Details");
                draw_input_string("Bank Name##bank", bank_name);
                draw_input_string("Account Holder##bank", account_holder);

                ImGui::Text("Banking Code Identifier Label:");
                ImGui::RadioButton("SWIFT Code", &swift_bic_type, 0);
                ImGui::SameLine();
                ImGui::RadioButton("BIC Code", &swift_bic_type, 1);
                ImGui::SameLine();
                ImGui::RadioButton("SWIFT / BIC Code", &swift_bic_type, 2);

                std::string const field_label = get_swift_bic_label() + "##bank";
                draw_input_string(field_label.c_str(), swift_code);
                draw_input_string("IBAN / Account No.##bank", iban_account);

                ui.spacing();
                ui.separator();
                ui.text_colored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Foreign Performance Declaration");
                draw_input_string("Declaration Statement##legal", foreign_statement);

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ui.spacing();
        ui.separator();
        ui.spacing();

        ui.text_colored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "PDF Export Settings");
        draw_input_string("PDF Output Path##path", pdf_output_path);

        if (ImGui::Button("Generate PDF Invoice", ImVec2(180, 32))) {
            std::string err;
            if (generate_pdf(pdf_output_path, err)) {
                status_message = "PDF Invoice successfully generated at: " + pdf_output_path;
                status_is_error = false;
                "say"_sfn("Invoice generated successfully");
            } else {
                status_message = err;
                status_is_error = true;
            }
        }

        if (!pdf_output_path.empty() && std::filesystem::exists(pdf_output_path)) {
            ImGui::SameLine();
            if (ImGui::Button("Open in PDF Viewer Card", ImVec2(180, 32))) {
                "create_card"_sfn("pdf:" + pdf_output_path);
            }
        }

        if (!status_message.empty()) {
            ui.spacing();
            if (status_is_error) {
                ui.text_colored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), status_message);
            } else {
                ui.text_colored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), status_message);
            }
        }

        render_contact_picker_modal(ui);
    });
}

std::vector<card::mcp_function> invoice_card::get_mcp_functions() const {
    return {
        mcp_function(
            "generate_invoice_pdf",
            "Generate a PDF invoice file with specified output path and invoice details.",
            R"mcp({"type":"object","properties":{"output_path":{"type":"string","description":"Destination PDF file path"}},"required":["output_path"]})mcp",
            [this](const std::string& params) -> std::string {
                std::string path = pdf_output_path;
                auto start = params.find("\"output_path\"");
                if (start != std::string::npos) {
                    start = params.find(':', start);
                    if (start != std::string::npos) {
                        start = params.find('"', start);
                        if (start != std::string::npos) {
                            start++;
                            auto end = params.find('"', start);
                            if (end != std::string::npos) {
                                path = params.substr(start, end - start);
                            }
                        }
                    }
                }

                std::string err;
                if (generate_pdf(path, err)) {
                    return std::format(R"({{"status": "success", "path": "{}"}})", path);
                }
                return std::format(R"({{"status": "error", "message": "{}"}})", err);
            }
        ),
        mcp_function(
            "set_provider_from_contact",
            "Set the invoice seller/provider information using a contact matched by search query or name.",
            R"mcp({"type":"object","properties":{"query":{"type":"string","description":"Contact name, email, or search term"}},"required":["query"]})mcp",
            [this](const std::string& params) -> std::string {
                std::string query;
                auto start = params.find("\"query\"");
                if (start != std::string::npos) {
                    start = params.find(':', start);
                    if (start != std::string::npos) {
                        start = params.find('"', start);
                        if (start != std::string::npos) {
                            start++;
                            auto end = params.find('"', start);
                            if (end != std::string::npos) {
                                query = params.substr(start, end - start);
                            }
                        }
                    }
                }

                if (query.empty()) {
                    return R"({"status": "error", "message": "query parameter missing"})";
                }

                const_cast<invoice_card*>(this)->refresh_contacts();
                auto matched = contacts_repo_ ? contacts_repo_->search_contacts(query) : std::vector<models::contacts::contact>{};
                if (matched.empty()) {
                    return std::format(R"({{"status": "error", "message": "No contact found matching '{}'"}})", query);
                }

                const_cast<invoice_card*>(this)->apply_contact_as_provider(matched.front());
                return std::format(R"({{"status": "success", "provider_name": "{}", "email": "{}"}})", seller_name, seller_email);
            }
        ),
        mcp_function(
            "set_customer_from_contact",
            "Set the invoice client/customer information using a contact matched by search query or name.",
            R"mcp({"type":"object","properties":{"query":{"type":"string","description":"Contact name, organization, email, or search term"}},"required":["query"]})mcp",
            [this](const std::string& params) -> std::string {
                std::string query;
                auto start = params.find("\"query\"");
                if (start != std::string::npos) {
                    start = params.find(':', start);
                    if (start != std::string::npos) {
                        start = params.find('"', start);
                        if (start != std::string::npos) {
                            start++;
                            auto end = params.find('"', start);
                            if (end != std::string::npos) {
                                query = params.substr(start, end - start);
                            }
                        }
                    }
                }

                if (query.empty()) {
                    return R"({"status": "error", "message": "query parameter missing"})";
                }

                const_cast<invoice_card*>(this)->refresh_contacts();
                auto matched = contacts_repo_ ? contacts_repo_->search_contacts(query) : std::vector<models::contacts::contact>{};
                if (matched.empty()) {
                    return std::format(R"({{"status": "error", "message": "No contact found matching '{}'"}})", query);
                }

                const_cast<invoice_card*>(this)->apply_contact_as_customer(matched.front());
                return std::format(R"({{"status": "success", "customer_name": "{}", "contact": "{}"}})", client_name, client_contact);
            }
        )
    };
}

} // namespace rouen::cards

