#pragma once

#include "../../helpers/imgui_include.hpp"
#include <string>
#include <memory>
#include <format>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "../interface/card.hpp"
#include "../../helpers/debug.hpp"
#include "../../hosts/bybit_host.hpp"
#include "../../registrar.hpp"

namespace rouen::cards {

class bybit_assets : public card {
public:
    bybit_assets() {
        // Set custom colors for the Bybit card
        colors[0] = {0.9f, 0.6f, 0.1f, 1.0f}; // Orange primary color (Bybit brand-like)
        colors[1] = {1.0f, 0.7f, 0.2f, 0.7f}; // Lighter orange secondary color
        
        // Additional colors for specific elements
        get_color(2, ImVec4(1.0f, 0.8f, 0.3f, 1.0f)); // Light orange for titles
        get_color(3, ImVec4(0.3f, 0.8f, 0.3f, 1.0f)); // Green for positive values
        get_color(4, ImVec4(0.8f, 0.4f, 0.4f, 1.0f)); // Red for negative values
        get_color(5, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Light gray for secondary text
        get_color(6, ImVec4(0.9f, 0.9f, 0.3f, 1.0f)); // Yellow for warnings/alerts
        
        name("Bybit Assets");
        width = 500.0f;
        requested_fps = 1;  // Update once per second for moderate refresh
        
        // Initialize the Bybit host
        bybit_host = std::make_shared<hosts::BybitHost>();
        
        // Initialize input buffers
        std::memset(api_key_buffer, 0, sizeof(api_key_buffer));
        std::memset(api_secret_buffer, 0, sizeof(api_secret_buffer));
        
        DB_INFO("Bybit Assets card: Constructor completed");
    }
    
    ~bybit_assets() override = default;

    bool render() override {
        return render_window([this]() {
            // Configuration section
            if (!bybit_host->hasCredentials()) {
                render_configuration();
            } else {
                render_assets();
            }
            
            ImGui::Separator();
            
            // Settings button to reconfigure
            if (ImGui::Button("Settings")) {
                show_settings = !show_settings;
            }
            
            if (show_settings) {
                ImGui::Separator();
                render_configuration();
            }
        });
    }

    std::string get_uri() const override {
        return "bybit-assets";
    }
    
private:
    std::shared_ptr<hosts::BybitHost> bybit_host;
    char api_key_buffer[256];
    char api_secret_buffer[256];
    bool show_settings = false;
    std::string last_error;
    
    void render_configuration() {
        ImGui::TextColored(colors[2], "Bybit API Configuration");
        ImGui::TextColored(colors[5], "Enter your Bybit API credentials to view your assets");
        
        ImGui::Spacing();
        ImGui::TextColored(colors[6], "Setup Instructions:");
        ImGui::BulletText("Go to Bybit > Account & Security > API Management");
        ImGui::BulletText("Create a new API key with 'Read' permissions");
        ImGui::BulletText("Select 'Wallet' and 'Spot Trading' permissions");
        ImGui::BulletText("Copy the API key and secret below");
        
        ImGui::Spacing();
        
        ImGui::Text("API Key:");
        ImGui::InputText("##api_key", api_key_buffer, sizeof(api_key_buffer), ImGuiInputTextFlags_Password);
        
        ImGui::Text("API Secret:");
        ImGui::InputText("##api_secret", api_secret_buffer, sizeof(api_secret_buffer), ImGuiInputTextFlags_Password);
        
        ImGui::Spacing();
        
        if (ImGui::Button("Save Credentials")) {
            if (strlen(api_key_buffer) > 0 && strlen(api_secret_buffer) > 0) {
                bybit_host->setCredentials(std::string(api_key_buffer), std::string(api_secret_buffer));
                show_settings = false;
                last_error.clear();
            } else {
                last_error = "Please enter both API key and secret";
            }
        }
        
        if (!last_error.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(colors[4], "Error: %s", last_error.c_str());
        }
        
        ImGui::Spacing();
        ImGui::TextColored(colors[6], "⚠️ Security Notice:");
        ImGui::TextColored(colors[5], "• API credentials are stored in memory only");
        ImGui::TextColored(colors[5], "• Never share your API secret with anyone");
        ImGui::TextColored(colors[5], "• Use read-only permissions for safety");
    }
    
    void render_assets() {
        auto account_info = bybit_host->getAccountInfo();
        
        if (!account_info) {
            ImGui::TextColored(colors[4], "Failed to fetch account information");
            std::string error = bybit_host->getLastError();
            if (!error.empty()) {
                ImGui::TextWrapped("%s", error.c_str());
                ImGui::Spacing();
            }
            
            ImGui::TextColored(colors[6], "Common issues:");
            ImGui::BulletText("Check your API key and secret");
            ImGui::BulletText("Ensure API key has wallet read permissions");
            ImGui::BulletText("Verify your Bybit account type");
            ImGui::BulletText("Check network connectivity");
            
            if (ImGui::Button("Retry")) {
                // Force refresh by calling the API again
                bybit_host->getAccountInfo();
            }
            return;
        }
        
        // Account summary
        ImGui::TextColored(colors[2], "Account Summary");
        ImGui::Separator();
        
        double total_equity = account_info->getTotalEquity();
        double total_wallet_balance = account_info->getTotalWalletBalance();
        
        ImGui::Text("Total Equity:");
        ImGui::SameLine();
        ImGui::TextColored(colors[3], "$%.2f", total_equity);
        
        ImGui::Text("Total Wallet Balance:");
        ImGui::SameLine();
        ImGui::TextColored(colors[3], "$%.2f", total_wallet_balance);
        
        ImGui::Text("Account Type:");
        ImGui::SameLine();
        ImGui::TextColored(colors[5], "%s", account_info->accountType.c_str());
        
        ImGui::Spacing();
        ImGui::TextColored(colors[2], "Asset Breakdown");
        ImGui::Separator();
        
        // Asset table
        if (ImGui::BeginTable("assets_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable)) {
            ImGui::TableSetupColumn("Coin", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Wallet Balance", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Transfer Balance", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            
            for (const auto& coin : account_info->coin) {
                double wallet_balance = coin.getWalletBalance();
                double transfer_balance = coin.getTransferBalance();
                
                // Only show coins with non-zero balances
                if (wallet_balance > 0.0001 || transfer_balance > 0.0001) {
                    ImGui::TableNextRow();
                    
                    // Coin name
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", coin.coin.c_str());
                    
                    // Wallet balance
                    ImGui::TableSetColumnIndex(1);
                    if (wallet_balance > 0.0001) {
                        ImGui::TextColored(colors[3], "%.6f", wallet_balance);
                    } else {
                        ImGui::TextColored(colors[5], "0.000000");
                    }
                    
                    // Transfer balance
                    ImGui::TableSetColumnIndex(2);
                    if (transfer_balance > 0.0001) {
                        ImGui::TextColored(colors[3], "%.6f", transfer_balance);
                    } else {
                        ImGui::TextColored(colors[5], "0.000000");
                    }
                }
            }
            
            ImGui::EndTable();
        }
        
        ImGui::Spacing();
        
        // Refresh button
        if (ImGui::Button("Refresh")) {
            // Force refresh by calling the API again
            bybit_host->getAccountInfo();
        }
        
        ImGui::SameLine();
        
        // Show last update time
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
        ImGui::TextColored(colors[5], "Last checked: %s", ss.str().c_str());
    }
};

} // namespace rouen::cards
