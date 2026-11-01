#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "../interface/card.hpp"
#include "../../models/contacts/contacts_repository.hpp"
#include "../../helpers/glaze_include.hpp"

namespace rouen::cards {

struct invoice_line_item {
    std::string description;
    double quantity{1.0};
    double unit_price{0.0};

    double amount() const {
        return quantity * unit_price;
    }
};

struct invoice_card_params {
    std::string seller_name;
    std::string seller_email;
    std::string seller_address;
    std::string seller_phone;
    std::string client_name;
    std::string client_contact;
    std::string client_address;
    std::string invoice_number;
    std::string invoice_date;
    std::string payment_terms;
    std::string currency;
    std::string bank_name;
    std::string account_holder;
    int swift_bic_type{0};
    std::string swift_code;
    std::string iban_account;
    std::string foreign_statement;
    uint64_t updated_at_ms{0};
};

} // namespace rouen::cards

namespace glz {
    template<> struct meta<rouen::cards::invoice_line_item> {
        using T = rouen::cards::invoice_line_item;
        static constexpr auto value = object(&T::description, &T::quantity, &T::unit_price);
    };
    template<> struct meta<rouen::cards::invoice_card_params> {
        using T = rouen::cards::invoice_card_params;
        static constexpr auto value = object(
            &T::seller_name, &T::seller_email, &T::seller_address, &T::seller_phone,
            &T::client_name, &T::client_contact, &T::client_address,
            &T::invoice_number, &T::invoice_date, &T::payment_terms, &T::currency,
            &T::bank_name, &T::account_holder, &T::swift_bic_type, &T::swift_code,
            &T::iban_account, &T::foreign_statement, &T::updated_at_ms
        );
    };
}

namespace rouen::cards {

enum class contact_picker_target {
    provider,
    customer
};

struct invoice_card : public card {
    invoice_card();

    std::string get_uri() const override {
        return "invoice";
    }

    std::string get_adaptive_card_json() const override;
    void handle_action(std::string_view action_json) override;

    bool render(rouen::ui::ui_context& ui) override;

    std::vector<card::mcp_function> get_mcp_functions() const override;

    bool generate_pdf(const std::string& output_path, std::string& error_msg) const;

    void apply_monthly_retainer();

    void refresh_contacts();
    void apply_contact_as_provider(const rouen::models::contacts::contact& c);
    void apply_contact_as_customer(const rouen::models::contacts::contact& c);
    void load_last_parameters();
    void save_last_parameters(bool publish_to_mesh = true);

private:
    // Seller / Provider Information
    std::string seller_name{"Ignacio Rodriguez"};
    std::string seller_email{"ignacio@example.com"};
    std::string seller_address{"Buenos Aires, Argentina"};
    std::string seller_phone{"+54 9 11 1234-5678"};

    // Client / Company Information
    std::string client_name{"Acme Corporation"};
    std::string client_contact{"billing@acme.com"};
    std::string client_address{"100 Tech Way, San Francisco, CA 94105, USA"};

    // Invoice Header Metadata
    std::string invoice_number{"INV-2026-001"};
    std::string invoice_date{"2026-07-25"};
    std::string payment_terms{"Net 30"};
    std::string currency{"USD"};

    // Line Items
    std::vector<invoice_line_item> items{
        {"Monthly Consulting Retainer - July 2026", 1.0, 10000.00}
    };

    // Bank & International Payment Details
    std::string bank_name{"Global International Bank"};
    std::string account_holder{"Ignacio Rodriguez"};
    int swift_bic_type{0}; // 0: SWIFT Code, 1: BIC Code, 2: SWIFT / BIC Code
    std::string swift_code{"GLOBUS33XXX"};
    std::string iban_account{"AR54 0123 4567 8901 2345 6789 01"};

    // Monthly Retainer Config
    int retainer_month_idx{6}; // 0 = Jan, 6 = July
    int retainer_year{2026};
    double retainer_amount{10000.00};

    // Foreign Performance Declaration
    std::string foreign_statement{"The services described herein were performed entirely outside the United States."};

    // Export state
    std::string pdf_output_path;
    std::string status_message;
    bool status_is_error{false};

    // Contacts Integration
    std::unique_ptr<rouen::models::contacts::contacts_repository> contacts_repo_;
    std::vector<rouen::models::contacts::contact> cached_contacts_;
    bool contacts_loaded_{false};

    // Contact picker UI state
    bool show_contact_picker_{false};
    contact_picker_target picker_target_{contact_picker_target::provider};
    char contact_search_buf_[128]{""};
    int selected_provider_combo_idx_{-1};
    int selected_customer_combo_idx_{-1};

    std::string get_swift_bic_label() const;
    double calculate_subtotal() const;
    double calculate_total() const;

    void render_contact_picker_modal(rouen::ui::ui_context& ui);
};

} // namespace rouen::cards
