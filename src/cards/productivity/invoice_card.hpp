#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "../interface/card.hpp"

namespace rouen::cards {

struct invoice_line_item {
    std::string description;
    double quantity{1.0};
    double unit_price{0.0};

    double amount() const {
        return quantity * unit_price;
    }
};

struct invoice_card : public card {
    invoice_card();

    std::string get_uri() const override {
        return "invoice";
    }

    bool render(rouen::ui::ui_context& ui) override;

    std::vector<card::mcp_function> get_mcp_functions() const override;

    bool generate_pdf(const std::string& output_path, std::string& error_msg) const;

    void apply_monthly_retainer();

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

    std::string get_swift_bic_label() const;
    double calculate_subtotal() const;
    double calculate_total() const;
};

} // namespace rouen::cards
