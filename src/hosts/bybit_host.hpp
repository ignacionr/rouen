#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <cstdlib>
#include "../helpers/glaze_include.hpp"
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <iomanip>

#include "../helpers/fetch.hpp"
#include "../helpers/debug.hpp"

#define BYBIT_ERROR(message) LOG_COMPONENT("BYBIT", LOG_LEVEL_ERROR, message)
#define BYBIT_ERROR_FMT(fmt, ...) BYBIT_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define BYBIT_WARN(message) LOG_COMPONENT("BYBIT", LOG_LEVEL_WARN, message)
#define BYBIT_WARN_FMT(fmt, ...) BYBIT_WARN(debug::format_log(fmt, __VA_ARGS__))
#define BYBIT_INFO(message) LOG_COMPONENT("BYBIT", LOG_LEVEL_INFO, message)
#define BYBIT_INFO_FMT(fmt, ...) BYBIT_INFO(debug::format_log(fmt, __VA_ARGS__))
#define BYBIT_DEBUG(message) LOG_COMPONENT("BYBIT", LOG_LEVEL_DEBUG, message)
#define BYBIT_DEBUG_FMT(fmt, ...) BYBIT_DEBUG(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::hosts {

// Wallet types supported by Bybit
enum class WalletType {
    UNIFIED,  // Unified Trading Account
    FUND,     // Funding Wallet
    EARN      // Earn Wallet
};

// Convert WalletType to string for API calls
std::string walletTypeToString(WalletType type) {
    switch (type) {
        case WalletType::UNIFIED: return "UNIFIED";
        case WalletType::FUND: return "FUND";
        case WalletType::EARN: return "EARN";
        default: return "UNIFIED";
    }
}

namespace bybit {
    // Bybit API response structure for account balances
    struct CoinBalance {
        std::string coin;
        std::string walletBalance;
        std::string transferBalance;
        std::string bonus;
        
        // Parse numbers for calculations
        double getWalletBalance() const {
            try {
                return std::stod(walletBalance);
            } catch (...) {
                return 0.0;
            }
        }
        
        double getTransferBalance() const {
            try {
                return std::stod(transferBalance);
            } catch (...) {
                return 0.0;
            }
        }
    };

    // The actual account data is nested inside a list array
    struct AccountData {
        std::string totalEquity;
        std::string totalWalletBalance;
        std::string totalMarginBalance;
        std::string totalAvailableBalance;
        std::string totalPerpUPL;
        std::string totalInitialMargin;
        std::string totalMaintenanceMargin;
        std::string accountType;
        std::vector<CoinBalance> coin;
        
        double getTotalEquity() const {
            try {
                return std::stod(totalEquity);
            } catch (...) {
                return 0.0;
            }
        }
        
        double getTotalWalletBalance() const {
            try {
                return std::stod(totalWalletBalance);
            } catch (...) {
                return 0.0;
            }
        }
    };

    struct AccountInfo {
        std::vector<AccountData> list;
    };

    struct BybitResponse {
        long long retCode;
        std::string retMsg;
        std::optional<AccountInfo> result;  // Make result optional to handle error cases
        glz::json_t retExtInfo;
        long long time;
    };

    // Asset API response structure for FUND and EARN wallets
    struct AssetBalance {
        std::string coin;
        std::string transferBalance;
        std::string walletBalance;
        std::string bonus;
        
        double getWalletBalance() const {
            try {
                return std::stod(walletBalance);
            } catch (...) {
                return 0.0;
            }
        }
        
        double getTransferBalance() const {
            try {
                return std::stod(transferBalance);
            } catch (...) {
                return 0.0;
            }
        }
    };

    struct AssetAccountInfo {
        std::string accountType;
        std::string memberId;
        std::vector<AssetBalance> balance;
    };

    struct AssetResponse {
        long long retCode;
        std::string retMsg;
        std::optional<AssetAccountInfo> result;
        glz::json_t retExtInfo;
        long long time;
    };

    // Unified data structure to hold either account or asset data
    struct WalletData {
        std::string accountType;
        std::string totalEquity = "0";
        std::string totalWalletBalance = "0";
        std::vector<CoinBalance> coins;
        
        // Default constructor
        WalletData() = default;
        
        // Constructor for AccountData
        explicit WalletData(const AccountData& account_data) 
            : accountType(account_data.accountType)
            , totalEquity(account_data.totalEquity)
            , totalWalletBalance(account_data.totalWalletBalance)
            , coins(account_data.coin) {}
        
        // Constructor for AssetAccountInfo
        explicit WalletData(const AssetAccountInfo& asset_info)
            : accountType(asset_info.accountType) {
            // Convert AssetBalance to CoinBalance
            for (const auto& asset_balance : asset_info.balance) {
                CoinBalance coin_balance;
                coin_balance.coin = asset_balance.coin;
                coin_balance.walletBalance = asset_balance.walletBalance;
                coin_balance.transferBalance = asset_balance.transferBalance;
                coin_balance.bonus = asset_balance.bonus;
                coins.push_back(coin_balance);
            }
            
            // For FUND/EARN wallets, we cannot meaningfully sum different cryptocurrencies
            // without currency conversion. Leave totalWalletBalance as "0" to indicate
            // that individual coin values should be displayed instead.
            totalWalletBalance = "0";
        }
        
        double getTotalEquity() const {
            try {
                return std::stod(totalEquity);
            } catch (...) {
                return 0.0;
            }
        }
        
        double getTotalWalletBalance() const {
            try {
                return std::stod(totalWalletBalance);
            } catch (...) {
                return 0.0;
            }
        }
    };

} // namespace bybit

/**
 * Bybit API Host Controller
 * 
 * This class manages communication with Bybit's REST API for account information.
 * It provides methods for fetching account balances and asset information.
 */
class BybitHost {
public:
    /**
     * Constructor initializes the Bybit host and loads API credentials from environment
     */
    BybitHost() {
        BYBIT_INFO("BybitHost constructor starting...");
        
        // Load API credentials from environment variables
        const char* api_key_env = std::getenv("BYBIT_API_KEY");
        const char* api_secret_env = std::getenv("BYBIT_API_SECRET");
        
        if (api_key_env && api_secret_env) {
            api_key_ = std::string(api_key_env);
            api_secret_ = std::string(api_secret_env);
            BYBIT_INFO("Loaded API credentials from environment variables");
        } else {
            BYBIT_WARN("BYBIT_API_KEY and/or BYBIT_API_SECRET environment variables not found");
        }
        
        BYBIT_INFO("BybitHost constructor completed");
    }

    /**
     * Destructor
     */
    ~BybitHost() {
        BYBIT_INFO("BybitHost destructor");
    }

    /**
     * Set API credentials
     */
    void setCredentials(const std::string& api_key, const std::string& api_secret) {
        std::lock_guard<std::mutex> lock(mutex_);
        api_key_ = api_key;
        api_secret_ = api_secret;
        BYBIT_INFO("API credentials set");
    }

    /**
     * Check if credentials are configured
     */
    bool hasCredentials() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !api_key_.empty() && !api_secret_.empty();
    }

    /**
     * Fetch wallet information from Bybit API for a specific wallet type
     */
    std::optional<bybit::WalletData> getWalletInfo(WalletType walletType = WalletType::UNIFIED) {
        if (!hasCredentials()) {
            BYBIT_WARN("No API credentials configured");
            return std::nullopt;
        }

        // Check cache based on wallet type
        auto cache_key = static_cast<int>(walletType);
        auto now = std::chrono::steady_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto cache_it = cached_wallet_info_.find(cache_key);
            auto last_update_it = last_update_per_wallet_.find(cache_key);
            
            if (cache_it != cached_wallet_info_.end() && 
                last_update_it != last_update_per_wallet_.end() &&
                now - last_update_it->second < std::chrono::seconds(30)) {
                return cache_it->second;
            }
        }

        if (walletType == WalletType::UNIFIED) {
            return fetchUnifiedWallet();
        } else {
            return fetchAssetWallet(walletType);
        }
    }

    /**
     * Fetch account information from Bybit API for a specific wallet type (deprecated)
     * Use getWalletInfo instead for unified interface
     */
    std::optional<bybit::AccountData> getAccountInfo(WalletType walletType = WalletType::UNIFIED) {
        if (walletType != WalletType::UNIFIED) {
            BYBIT_WARN_FMT("{} wallet type is not supported by AccountData structure. Use getWalletInfo() instead.", walletTypeToString(walletType));
            return std::nullopt;
        }

        auto wallet = fetchUnifiedWallet();
        if (wallet.has_value()) {
            // Convert WalletData to AccountData for backward compatibility
            bybit::AccountData account_data;
            account_data.accountType = wallet->accountType;
            account_data.totalEquity = wallet->totalEquity;
            account_data.totalWalletBalance = wallet->totalWalletBalance;
            account_data.coin = wallet->coins;
            return account_data;
        }
        return std::nullopt;
    }

private:
    /**
     * Fetch UNIFIED wallet data using the account/wallet-balance endpoint
     */
    std::optional<bybit::WalletData> fetchUnifiedWallet() {
        auto cache_key = static_cast<int>(WalletType::UNIFIED);
        auto now = std::chrono::steady_clock::now();

        try {
            std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            
            std::string queryString = "accountType=UNIFIED";
            std::string signature = generateSignature(queryString);
            
            BYBIT_DEBUG("Fetching UNIFIED wallet info");
            BYBIT_DEBUG_FMT("API Key: {} (length: {})", api_key_.substr(0, 10) + "...", api_key_.length());
            BYBIT_DEBUG_FMT("Query String: {}", queryString);
            BYBIT_DEBUG_FMT("Signature: {} (length: {})", signature.substr(0, 16) + "...", signature.length());
            
            std::string url = "https://api.bybit.com/v5/account/wallet-balance?" + queryString;

            http::fetch fetcher(30); // 30 second timeout
            std::string response = fetcher(url, [this, timestamp, signature](auto set_header) {
                set_header("X-BAPI-API-KEY: " + api_key_);
                set_header("X-BAPI-TIMESTAMP: " + timestamp);
                set_header("X-BAPI-RECV-WINDOW: 5000");
                set_header("X-BAPI-SIGN: " + signature);
                set_header("Content-Type: application/json");
            });

            BYBIT_DEBUG_FMT("Raw Bybit API response for UNIFIED: {}", response);

            // Try to parse the structured response directly
            bybit::BybitResponse bybit_response;
            auto parse_result = glz::read<glz::opts{.error_on_unknown_keys=false}>(bybit_response, response);
            
            if (parse_result) {
                last_error_ = "Failed to parse account info structure: " + std::string(glz::format_error(parse_result, response));
                BYBIT_ERROR_FMT("Failed to parse account info: {}", last_error_);
                return std::nullopt;
            }

            // Check if the API returned an error
            if (bybit_response.retCode != 0) {
                last_error_ = "Bybit API error: " + std::to_string(bybit_response.retCode) + " - " + bybit_response.retMsg;
                BYBIT_ERROR(last_error_);
                return std::nullopt;
            }

            // Check if result exists and has data
            if (!bybit_response.result.has_value() || bybit_response.result->list.empty()) {
                last_error_ = "Empty or missing result from Bybit API - check your API permissions for UNIFIED";
                BYBIT_WARN(last_error_);
                return std::nullopt;
            }

            // Cache the result - get the first account data from the list
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!bybit_response.result->list.empty()) {
                    bybit::WalletData wallet_data(bybit_response.result->list[0]);
                    cached_wallet_info_[cache_key] = wallet_data;
                    last_update_per_wallet_[cache_key] = now;
                    last_error_.clear();
                    
                    BYBIT_INFO("Successfully fetched UNIFIED wallet information from Bybit");
                    return wallet_data;
                } else {
                    last_error_ = "No account data in response for UNIFIED";
                    BYBIT_WARN(last_error_);
                    return std::nullopt;
                }
            }

        } catch (const std::exception& e) {
            last_error_ = "Exception: " + std::string(e.what());
            BYBIT_ERROR_FMT("Exception fetching UNIFIED wallet info: {}", e.what());
            return std::nullopt;
        }
    }

    /**
     * Fetch FUND or EARN wallet data using the asset endpoint
     */
    std::optional<bybit::WalletData> fetchAssetWallet(WalletType walletType) {
        // Check if the wallet type is supported
        if (walletType == WalletType::EARN) {
            last_error_ = "EARN wallet type is not supported by Bybit's current API endpoints";
            BYBIT_WARN(last_error_);
            return std::nullopt;
        }
        
        auto cache_key = static_cast<int>(walletType);
        auto now = std::chrono::steady_clock::now();

        try {
            std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            
            std::string queryString = "accountType=" + walletTypeToString(walletType);
            std::string signature = generateSignature(queryString);
            
            BYBIT_DEBUG_FMT("Fetching {} wallet info", walletTypeToString(walletType));
            BYBIT_DEBUG_FMT("API Key: {} (length: {})", api_key_.substr(0, 10) + "...", api_key_.length());
            BYBIT_DEBUG_FMT("Query String: {}", queryString);
            BYBIT_DEBUG_FMT("Signature: {} (length: {})", signature.substr(0, 16) + "...", signature.length());
            
            std::string url = "https://api.bybit.com/v5/asset/transfer/query-account-coins-balance?" + queryString;

            http::fetch fetcher(30); // 30 second timeout
            std::string response = fetcher(url, [this, timestamp, signature](auto set_header) {
                set_header("X-BAPI-API-KEY: " + api_key_);
                set_header("X-BAPI-TIMESTAMP: " + timestamp);
                set_header("X-BAPI-RECV-WINDOW: 5000");
                set_header("X-BAPI-SIGN: " + signature);
                set_header("Content-Type: application/json");
            });

            BYBIT_DEBUG_FMT("Raw Bybit API response for {}: {}", walletTypeToString(walletType), response);

            // Try to parse the structured response directly
            bybit::AssetResponse asset_response;
            auto parse_result = glz::read<glz::opts{.error_on_unknown_keys=false}>(asset_response, response);
            
            if (parse_result) {
                last_error_ = "Failed to parse asset info structure: " + std::string(glz::format_error(parse_result, response));
                BYBIT_ERROR_FMT("Failed to parse asset info: {}", last_error_);
                return std::nullopt;
            }

            // Check if the API returned an error
            if (asset_response.retCode != 0) {
                // Specific handling for unsupported account types
                if (asset_response.retCode == 131203) {
                    last_error_ = "Wallet type " + walletTypeToString(walletType) + " is not supported by the asset endpoint (error 131203)";
                } else {
                    last_error_ = "Bybit API error: " + std::to_string(asset_response.retCode) + " - " + asset_response.retMsg;
                }
                BYBIT_ERROR(last_error_);
                return std::nullopt;
            }

            // Check if result exists
            if (!asset_response.result.has_value()) {
                last_error_ = "Empty or missing result from Bybit Asset API - check your API permissions for " + walletTypeToString(walletType);
                BYBIT_WARN(last_error_);
                return std::nullopt;
            }

            // Cache the result
            {
                std::lock_guard<std::mutex> lock(mutex_);
                bybit::WalletData wallet_data(asset_response.result.value());
                cached_wallet_info_[cache_key] = wallet_data;
                last_update_per_wallet_[cache_key] = now;
                last_error_.clear();
                
                BYBIT_INFO_FMT("Successfully fetched {} wallet information from Bybit", walletTypeToString(walletType));
                return wallet_data;
            }

        } catch (const std::exception& e) {
            last_error_ = "Exception: " + std::string(e.what());
            BYBIT_ERROR_FMT("Exception fetching {} wallet info: {}", walletTypeToString(walletType), e.what());
            return std::nullopt;
        }
    }

public:

    /**
     * Get all available wallet balances for all supported wallet types
     */
    std::unordered_map<WalletType, bybit::WalletData> getAllWalletBalances() {
        std::unordered_map<WalletType, bybit::WalletData> wallets;
        
        // Fetch supported wallet types only
        // Note: EARN wallet type is not supported by Bybit's current API endpoints
        std::vector<WalletType> wallet_types = {WalletType::UNIFIED, WalletType::FUND};
        
        for (const auto& wallet_type : wallet_types) {
            auto wallet_data = getWalletInfo(wallet_type);
            if (wallet_data.has_value()) {
                wallets[wallet_type] = wallet_data.value();
            }
        }
        
        return wallets;
    }

    /**
     * Get unified wallet balances (backward compatibility)
     */
    std::unordered_map<WalletType, bybit::AccountData> getUnifiedWalletBalances() {
        std::unordered_map<WalletType, bybit::AccountData> wallets;
        
        // Only UNIFIED wallet can be converted to AccountData format
        auto account_data = getAccountInfo(WalletType::UNIFIED);
        if (account_data.has_value()) {
            wallets[WalletType::UNIFIED] = account_data.value();
        }
        
        return wallets;
    }

    /**
     * Get the last error message
     */
    std::string getLastError() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_error_;
    }

private:
    mutable std::mutex mutex_;
    std::string api_key_;
    std::string api_secret_;
    std::string last_error_;
    std::unordered_map<int, bybit::AccountData> cached_account_info_;  // Cache per wallet type (backward compatibility)
    std::unordered_map<int, bybit::WalletData> cached_wallet_info_;    // New unified cache per wallet type
    std::unordered_map<int, std::chrono::steady_clock::time_point> last_update_per_wallet_;  // Track updates per wallet
    std::chrono::steady_clock::time_point last_update_;  // Keep for backward compatibility

    /**
     * Generate HMAC-SHA256 signature for Bybit API
     */
    std::string generateSignature(const std::string& query_string) {
        // Get current timestamp in milliseconds
        std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        
        std::string recv_window = "5000";
        
        // Bybit signature payload: timestamp + api_key + recv_window + query_string
        std::string payload = timestamp + api_key_ + recv_window + query_string;
        
        BYBIT_DEBUG_FMT("Signature payload: {}", payload);
        BYBIT_DEBUG_FMT("API Secret length: {}", api_secret_.length());
        
        // Generate HMAC-SHA256
        int key_len = static_cast<int>(api_secret_.length());
        auto data_len = payload.length();
        unsigned char* digest = HMAC(EVP_sha256(), 
                                   api_secret_.c_str(), key_len,
                                   reinterpret_cast<const unsigned char*>(payload.c_str()), data_len,
                                   nullptr, nullptr);
        
        if (!digest) {
            BYBIT_ERROR("Failed to generate HMAC-SHA256 signature");
            return "";
        }
        
        // Convert to hex string
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
        }
        
        std::string signature = ss.str();
        BYBIT_DEBUG_FMT("Generated HMAC-SHA256 signature: {} (length: {})", signature, signature.length());
        
        return signature;
    }
};

} // namespace rouen::hosts

// Glaze metadata for JSON parsing
template <>
struct glz::meta<rouen::hosts::bybit::CoinBalance> {
    using T = rouen::hosts::bybit::CoinBalance;
    static constexpr auto value = object(
        "coin", &T::coin,
        "walletBalance", &T::walletBalance,
        "transferBalance", &T::transferBalance,
        "bonus", &T::bonus
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::bybit::AccountData> {
    using T = rouen::hosts::bybit::AccountData;
    static constexpr auto value = object(
        "totalEquity", &T::totalEquity,
        "totalWalletBalance", &T::totalWalletBalance,
        "totalMarginBalance", &T::totalMarginBalance,
        "totalAvailableBalance", &T::totalAvailableBalance,
        "totalPerpUPL", &T::totalPerpUPL,
        "totalInitialMargin", &T::totalInitialMargin,
        "totalMaintenanceMargin", &T::totalMaintenanceMargin,
        "accountType", &T::accountType,
        "coin", &T::coin
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::bybit::AccountInfo> {
    using T = rouen::hosts::bybit::AccountInfo;
    static constexpr auto value = object(
        "list", &T::list
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::bybit::BybitResponse> {
    using T = rouen::hosts::bybit::BybitResponse;
    static constexpr auto value = object(
        "retCode", &T::retCode,
        "retMsg", &T::retMsg,
        "result", &T::result,
        "retExtInfo", &T::retExtInfo,
        "time", &T::time
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false,
        .error_on_missing_keys = false  // Allow missing result field in error responses
    };
};

template <>
struct glz::meta<rouen::hosts::bybit::AssetBalance> {
    using T = rouen::hosts::bybit::AssetBalance;
    static constexpr auto value = object(
        "coin", &T::coin,
        "transferBalance", &T::transferBalance,
        "walletBalance", &T::walletBalance,
        "bonus", &T::bonus
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::bybit::AssetAccountInfo> {
    using T = rouen::hosts::bybit::AssetAccountInfo;
    static constexpr auto value = object(
        "accountType", &T::accountType,
        "memberId", &T::memberId,
        "balance", &T::balance
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::bybit::AssetResponse> {
    using T = rouen::hosts::bybit::AssetResponse;
    static constexpr auto value = object(
        "retCode", &T::retCode,
        "retMsg", &T::retMsg,
        "result", &T::result,
        "retExtInfo", &T::retExtInfo,
        "time", &T::time
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false,
        .error_on_missing_keys = false  // Allow missing result field in error responses
    };
};

template <>
struct glz::meta<rouen::hosts::bybit::WalletData> {
    using T = rouen::hosts::bybit::WalletData;
    static constexpr auto value = object(
        "accountType", &T::accountType,
        "totalEquity", &T::totalEquity,
        "totalWalletBalance", &T::totalWalletBalance,
        "coins", &T::coins
    );
    
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};
