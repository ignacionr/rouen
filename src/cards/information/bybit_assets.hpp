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
        get_color(7, ImVec4(0.6f, 0.8f, 1.0f, 1.0f)); // Light blue for wallet types
        
        name("Bybit Assets");
        width = 600.0f;  // Increased width to accommodate multiple wallets
        requested_fps = 1;  // Update once per second for moderate refresh
        
        // Initialize the Bybit host
        bybit_host = std::make_shared<hosts::BybitHost>();
        
        DB_INFO("Bybit Assets card: Constructor completed");
    }
    
    ~bybit_assets() override = default;

    bool render() override {
        return render_window([this]() {
            // Check if credentials are available from environment
            if (!bybit_host->hasCredentials()) {
                render_no_credentials();
            } else {
                render_assets();
            }
        });
    }

    std::string get_uri() const override {
        return "bybit-assets";
    }
    
private:
    std::shared_ptr<hosts::BybitHost> bybit_host;
    std::string last_error;
    
    void render_no_credentials() {
        ImGui::TextColored(colors[2], "Bybit API Configuration Required");
        ImGui::TextColored(colors[4], "BYBIT_API_KEY and BYBIT_API_SECRET environment variables not found");
        
        ImGui::Spacing();
        ImGui::TextColored(colors[6], "Setup Instructions:");
        ImGui::BulletText("Go to Bybit > Account & Security > API Management");
        ImGui::BulletText("Create a new API key with 'Read' permissions");
        ImGui::BulletText("Select 'Wallet' permissions for all account types");
        ImGui::BulletText("Set environment variables:");
        
        ImGui::Spacing();
        ImGui::TextColored(colors[5], "export BYBIT_API_KEY=\"your_api_key\"");
        ImGui::TextColored(colors[5], "export BYBIT_API_SECRET=\"your_api_secret\"");
        
        ImGui::Spacing();
        ImGui::TextColored(colors[6], "⚠️ Security Notice:");
        ImGui::TextColored(colors[5], "• Use read-only permissions for safety");
        ImGui::TextColored(colors[5], "• Never share your API secret with anyone");
        ImGui::TextColored(colors[5], "• Restart the application after setting environment variables");
    }
    
    void render_assets() {
        // Fetch all wallet balances
        auto all_wallets = bybit_host->getAllWalletBalances();
        
        if (all_wallets.empty()) {
            ImGui::TextColored(colors[4], "Failed to fetch account information");
            std::string error = bybit_host->getLastError();
            if (!error.empty()) {
                ImGui::TextWrapped("%s", error.c_str());
                ImGui::Spacing();
            }
            
            ImGui::TextColored(colors[6], "Common issues:");
            ImGui::BulletText("Check your environment variables (BYBIT_API_KEY, BYBIT_API_SECRET)");
            ImGui::BulletText("Ensure API key has wallet read permissions");
            ImGui::BulletText("Verify network connectivity");
            ImGui::BulletText("Check if API endpoints are accessible");
            
            ImGui::Spacing();
            ImGui::TextColored(colors[5], "Note: Currently only UNIFIED and FUND wallet types are supported");
            ImGui::TextColored(colors[5], "EARN wallets are not supported by Bybit's current API");
            return;
        }

        // Show info about supported wallet types
        ImGui::TextColored(colors[5], "Supported wallet types: UNIFIED Trading, FUND (Funding Wallet)");
        ImGui::Spacing();

        double total_portfolio_value = 0.0;
        
        // Display summary for each wallet type
        for (const auto& [wallet_type, wallet_data] : all_wallets) {
            std::string wallet_name;
            switch (wallet_type) {
                case rouen::hosts::WalletType::UNIFIED:
                    wallet_name = "🔄 Unified Trading";
                    break;
                case rouen::hosts::WalletType::FUND:
                    wallet_name = "💰 Funding Wallet";
                    break;
                case rouen::hosts::WalletType::EARN:
                    wallet_name = "📈 Earn Wallet (Not Supported)";
                    break;
                default:
                    wallet_name = "❓ Unknown Wallet";
                    break;
            }

            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.3f, 0.4f, 0.8f));
            if (ImGui::CollapsingHeader(wallet_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PopStyleColor();
                
                // Wallet summary
                ImGui::Indent();
                ImGui::TextColored(colors[0], "Account Type: %s", wallet_data.accountType.c_str());
                
                // Handle display differently based on wallet type
                if (wallet_type == rouen::hosts::WalletType::UNIFIED) {
                    // For UNIFIED wallets, use totalEquity which is already in USD
                    double equity = wallet_data.getTotalEquity();
                    if (equity > 0) {
                        ImGui::TextColored(colors[1], "Total Equity (USD): $%.2f", equity);
                        total_portfolio_value += equity;  // Only add USD values
                    }
                    
                    double wallet_balance = wallet_data.getTotalWalletBalance();
                    if (wallet_balance > 0) {
                        ImGui::TextColored(colors[2], "Total Wallet Balance (USD): $%.2f", wallet_balance);
                    }
                } else {
                    // For FUND wallets, show warning about currency conversion
                    ImGui::TextColored(colors[6], "⚠️ Individual coin values shown (no USD conversion available)");
                }
                
                ImGui::Spacing();
                
                // Individual coin balances
                if (!wallet_data.coins.empty()) {
                    ImGui::TextColored(colors[3], "Individual Holdings:");
                    
                    if (ImGui::BeginTable(("wallet_table_" + std::to_string(static_cast<int>(wallet_type))).c_str(), 3, 
                                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                        ImGui::TableSetupColumn("Coin", ImGuiTableColumnFlags_WidthFixed, 80);
                        ImGui::TableSetupColumn("Balance", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Transfer Available", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();
                        
                        for (const auto& coin : wallet_data.coins) {
                            double balance = coin.getWalletBalance();
                            double transferable = coin.getTransferBalance();
                            
                            if (balance > 0.0001 || transferable > 0.0001) { // Only show meaningful balances
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextColored(colors[0], "%s", coin.coin.c_str());
                                
                                ImGui::TableSetColumnIndex(1);
                                ImGui::Text("%.8f %s", balance, coin.coin.c_str());
                                
                                ImGui::TableSetColumnIndex(2);
                                ImGui::Text("%.8f %s", transferable, coin.coin.c_str());
                            }
                        }
                        ImGui::EndTable();
                    }
                } else {
                    ImGui::TextColored(colors[5], "No coins found in this wallet");
                }
                
                ImGui::Unindent();
                ImGui::Spacing();
            } else {
                ImGui::PopStyleColor();
            }
        }
        
        // Total portfolio summary
        ImGui::Separator();
        if (total_portfolio_value > 0) {
            ImGui::TextColored(colors[1], "Total USD Portfolio Value: $%.2f", total_portfolio_value);
            ImGui::TextColored(colors[5], "(Only includes UNIFIED wallet with USD conversion)");
        } else {
            ImGui::TextColored(colors[5], "USD Portfolio Total: Not available");
            ImGui::TextColored(colors[5], "(FUND wallet individual coin values shown above)");
        }
    }

    void render_wallet_section(const std::string& wallet_name, const rouen::hosts::bybit::WalletData& wallet_data) {
        ImGui::TextColored(colors[7], "%s", wallet_name.c_str());
        ImGui::Separator();
        
        double total_equity = wallet_data.getTotalEquity();
        double total_wallet_balance = wallet_data.getTotalWalletBalance();
        
        // Wallet summary
        ImGui::Text("Total Equity:");
        ImGui::SameLine();
        ImGui::TextColored(colors[3], "$%.2f", total_equity);
        
        ImGui::Text("Total Wallet Balance:");
        ImGui::SameLine();
        ImGui::TextColored(colors[3], "$%.2f", total_wallet_balance);
        
        // Show assets if there are any with non-zero balances
        bool has_assets = false;
        for (const auto& coin : wallet_data.coins) {
            if (coin.getWalletBalance() > 0.0001 || coin.getTransferBalance() > 0.0001) {
                has_assets = true;
                break;
            }
        }
        
        if (has_assets) {
            ImGui::Spacing();
            ImGui::TextColored(colors[5], "Assets:");
            
            // Asset table for this wallet
            std::string table_id = "assets_table_" + wallet_name;
            if (ImGui::BeginTable(table_id.c_str(), 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Coin", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Wallet Balance", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Transfer Balance", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableHeadersRow();
                
                for (const auto& coin : wallet_data.coins) {
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
        } else {
            ImGui::TextColored(colors[5], "No assets with significant balances");
        }
        
        ImGui::Spacing();
    }
};

} // namespace rouen::cards
