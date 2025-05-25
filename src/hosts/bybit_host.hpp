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
#include <glaze/glaze.hpp>
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
}

/**
 * Bybit API Host Controller
 * 
 * This class manages communication with Bybit's REST API for account information.
 * It provides methods for fetching account balances and asset information.
 */
class BybitHost {
public:
    /**
     * Constructor initializes the Bybit host
     */
    BybitHost() : last_update_(std::chrono::steady_clock::time_point::min()) {
        BYBIT_INFO("BybitHost constructor starting...");
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
     * Fetch account information from Bybit API
     */
    std::optional<bybit::AccountData> getAccountInfo() {
        if (!hasCredentials()) {
            BYBIT_WARN("No API credentials configured");
            return std::nullopt;
        }

        // Check if we should update (don't spam the API)
        auto now = std::chrono::steady_clock::now();
        if (now - last_update_ < std::chrono::seconds(30)) {
            // Return cached data if available
            std::lock_guard<std::mutex> lock(mutex_);
            if (cached_account_info_.has_value()) {
                return cached_account_info_;
            }
        }        try {
            std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            
            std::string queryString = "accountType=UNIFIED";
            std::string signature = generateSignature(queryString);
            
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

            BYBIT_DEBUG_FMT("Raw Bybit API response: {}", response);

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
                last_error_ = "Empty or missing result from Bybit API - check your API permissions";
                BYBIT_WARN(last_error_);
                return std::nullopt;
            }

            // Cache the result - get the first account data from the list
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!bybit_response.result->list.empty()) {
                    cached_account_info_ = bybit_response.result->list[0];
                    last_update_ = now;
                    last_error_.clear();
                } else {
                    last_error_ = "No account data in response";
                    BYBIT_WARN(last_error_);
                    return std::nullopt;
                }
            }

            BYBIT_INFO("Successfully fetched account information from Bybit");
            return cached_account_info_;

        } catch (const std::exception& e) {
            last_error_ = "Exception: " + std::string(e.what());
            BYBIT_ERROR_FMT("Exception fetching account info: {}", e.what());
            return std::nullopt;
        }
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
    std::optional<bybit::AccountData> cached_account_info_;
    std::chrono::steady_clock::time_point last_update_;

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
        unsigned char* digest = HMAC(EVP_sha256(), 
                                   api_secret_.c_str(), static_cast<int>(api_secret_.length()),
                                   reinterpret_cast<const unsigned char*>(payload.c_str()), static_cast<int>(payload.length()),
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
