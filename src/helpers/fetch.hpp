#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <curl/curl.h>
#include <sstream>
#include <memory>
#include <functional>

#include "debug.hpp"

#define HTTP_ERROR(message) LOG_COMPONENT("HTTP", LOG_LEVEL_ERROR, message)
#define HTTP_ERROR_FMT(fmt, ...) HTTP_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define HTTP_WARN(message) LOG_COMPONENT("HTTP", LOG_LEVEL_WARN, message)
#define HTTP_WARN_FMT(fmt, ...) HTTP_WARN(debug::format_log(fmt, __VA_ARGS__))
#define HTTP_INFO(message) LOG_COMPONENT("HTTP", LOG_LEVEL_INFO, message)
#define HTTP_INFO_FMT(fmt, ...) HTTP_INFO(debug::format_log(fmt, __VA_ARGS__))
#define HTTP_DEBUG(message) LOG_COMPONENT("HTTP", LOG_LEVEL_DEBUG, message)
#define HTTP_DEBUG_FMT(fmt, ...) HTTP_DEBUG(debug::format_log(fmt, __VA_ARGS__))

namespace http {

// CURL RAII wrapper
class curl_handle {
public:
    curl_handle() : handle(curl_easy_init()) {
        if (!handle) {
            throw std::runtime_error("Failed to initialize CURL");
        }
    }
    
    ~curl_handle() {
        if (handle) {
            curl_easy_cleanup(handle);
        }
    }
    
    // No copying
    curl_handle(const curl_handle&) = delete;
    curl_handle& operator=(const curl_handle&) = delete;
    
    // Access the underlying handle
    CURL* get() { return handle; }
    
private:
    CURL* handle;
};

// Default callback function for CURL to write data
static size_t write_callback(char* contents, size_t size, size_t nmemb, void* userp) {
    size_t real_size = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(contents, real_size);
    return real_size;
}

// HTTP client to fetch data from URLs with configurable timeout
class fetch {
public:
    // Use void* for content pointer to match how it's used in the codebase
    using WriteCallback = size_t (*)(void*, size_t, size_t, void*);
    
    // Function type for header setup (used by existing code)
    using HeaderSetter = std::function<void(const std::string&)>;
    
    // Constructor with default timeout
    fetch() : timeout_(30), connect_timeout_(10) {
        // Initialize CURL globally if not already initialized
        static bool curl_initialized = false;
        if (!curl_initialized) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curl_initialized = true;
            HTTP_INFO("CURL globally initialized");
        }
    }
    
    // Constructor with custom timeout
    explicit fetch(long timeout) : timeout_(timeout), connect_timeout_(timeout > 10 ? 10 : timeout) {
        // Initialize CURL globally if not already initialized
        static bool curl_initialized = false;
        if (!curl_initialized) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curl_initialized = true;
            HTTP_INFO("CURL globally initialized");
        }
        
        HTTP_INFO_FMT("Created fetch client with timeout: {}s", timeout);
    }
    
    // Basic GET request with vector of headers
    std::string operator()(
        const std::string& url, 
        const std::vector<std::string>& headers = {},
        WriteCallback custom_callback = nullptr,
        void* custom_data = nullptr
    ) {
        try {
            // Create a CURL handle
            curl_handle handle;
            
            // Response string to store the result (if using default callback)
            std::string response;
            
            // Set URL
            curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
            
            // Set write function and data
            if (custom_callback && custom_data) {
                curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, custom_callback);
                curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, custom_data);
            } else {
                curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, write_callback);
                curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response);
            }
            
            // Set timeouts
            curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, timeout_);
            curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, connect_timeout_);
            
            // Enable automatic redirect following
            curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 10L);
            
            // Set user agent
            curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "Rouen-HTTP/1.0");
            
            // Add custom headers if provided
            struct curl_slist* curl_headers = nullptr;
            if (!headers.empty()) {
                for (const auto& header : headers) {
                    curl_headers = curl_slist_append(curl_headers, header.c_str());
                }
                curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, curl_headers);
            }
            
            // Perform the request
            HTTP_INFO_FMT("Fetching URL: {}", url);
            CURLcode res = curl_easy_perform(handle.get());
            
            // Clean up headers if set
            if (curl_headers) {
                curl_slist_free_all(curl_headers);
            }
            
            // Check for errors
            if (res != CURLE_OK) {
                HTTP_ERROR_FMT("CURL request failed: {}", curl_easy_strerror(res));
                throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(res));
            }
            
            // Check HTTP status code
            long http_code = 0;
            curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &http_code);
            
            if (http_code >= 400) {
                HTTP_ERROR_FMT("HTTP error: {} ({})", http_code, url);
                throw std::runtime_error("HTTP error " + std::to_string(http_code));
            }
            
            // Only log response size if we're using our internal response string
            if (!custom_callback) {
                HTTP_INFO_FMT("Fetched {} bytes from {}", response.size(), url);
            } else {
                HTTP_INFO_FMT("Fetched data from {} using custom callback", url);
            }
            
            return response;
        } catch (const std::exception& e) {
            HTTP_ERROR_FMT("Exception during fetch: {}", e.what());
            throw;
        }
    }
    
    // GET with lambda for header setup (used by existing code)
    template<typename F>
    std::string operator()(
        const std::string& url,
        F header_setter,
        WriteCallback custom_callback = nullptr,
        void* custom_data = nullptr
    ) {
        try {
            // Create a CURL handle
            curl_handle handle;
            
            // Response string to store the result (if using default callback)
            std::string response;
            
            // Set URL
            curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
            
            // Set write function and data
            if (custom_callback && custom_data) {
                curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, custom_callback);
                curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, custom_data);
            } else {
                curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, write_callback);
                curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response);
            }
            
            // Set timeouts
            curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, timeout_);
            curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, connect_timeout_);
            
            // Enable automatic redirect following
            curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 10L);
            
            // Set user agent
            curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "Rouen-HTTP/1.0");
            
            // Setup headers using the provided setter
            struct curl_slist* curl_headers = nullptr;
            auto header_appender = [&curl_headers](const std::string& header) {
                curl_headers = curl_slist_append(curl_headers, header.c_str());
            };
            
            // Call the header setter with our header_appender function
            header_setter(header_appender);
            
            if (curl_headers) {
                curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, curl_headers);
            }
            
            // Perform the request
            HTTP_INFO_FMT("Fetching URL: {}", url);
            CURLcode res = curl_easy_perform(handle.get());
            
            // Clean up headers if set
            if (curl_headers) {
                curl_slist_free_all(curl_headers);
            }
            
            // Check for errors
            if (res != CURLE_OK) {
                HTTP_ERROR_FMT("CURL request failed: {}", curl_easy_strerror(res));
                throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(res));
            }
            
            // Check HTTP status code
            long http_code = 0;
            curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &http_code);
            
            if (http_code >= 400) {
                HTTP_ERROR_FMT("HTTP error: {} ({})", http_code, url);
                throw std::runtime_error("HTTP error " + std::to_string(http_code));
            }
            
            // Only log response size if we're using our internal response string
            if (!custom_callback) {
                HTTP_INFO_FMT("Fetched {} bytes from {}", response.size(), url);
            } else {
                HTTP_INFO_FMT("Fetched data from {} using custom callback", url);
            }
            
            return response;
        } catch (const std::exception& e) {
            HTTP_ERROR_FMT("Exception during fetch: {}", e.what());
            throw;
        }
    }
    
    // Basic POST request with vector of headers
    std::string post(
        const std::string& url, 
        const std::string& data,
        const std::vector<std::string>& headers = {},
        WriteCallback custom_callback = nullptr,
        void* custom_data = nullptr
    ) {
        try {
            // Create a CURL handle
            curl_handle handle;
            
            // Response string to store the result (if using default callback)
            std::string response;
            
            // Set URL
            curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
            
            // Set POST method and data
            curl_easy_setopt(handle.get(), CURLOPT_POST, 1L);
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, data.c_str());
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE, data.length());
            
            // Set write function and data
            if (custom_callback && custom_data) {
                curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, custom_callback);
                curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, custom_data);
            } else {
                curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, write_callback);
                curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response);
            }
            
            // Set timeouts
            curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, timeout_);
            curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, connect_timeout_);
            
            // Enable automatic redirect following
            curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 10L);
            
            // Set user agent
            curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "Rouen-HTTP/1.0");
            
            // Add custom headers if provided
            struct curl_slist* curl_headers = nullptr;
            if (!headers.empty()) {
                for (const auto& header : headers) {
                    curl_headers = curl_slist_append(curl_headers, header.c_str());
                }
                curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, curl_headers);
            }
            
            // Perform the request
            HTTP_INFO_FMT("Posting to URL: {}", url);
            CURLcode res = curl_easy_perform(handle.get());
            
            // Clean up headers if set
            if (curl_headers) {
                curl_slist_free_all(curl_headers);
            }
            
            // Check for errors
            if (res != CURLE_OK) {
                HTTP_ERROR_FMT("CURL POST request failed: {}", curl_easy_strerror(res));
                throw std::runtime_error(std::string("CURL POST request failed: ") + curl_easy_strerror(res));
            }
            
            // Check HTTP status code
            long http_code = 0;
            curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &http_code);
            
            if (http_code >= 400) {
                HTTP_ERROR_FMT("HTTP error: {} ({})", http_code, url);
                throw std::runtime_error("HTTP error " + std::to_string(http_code));
            }
            
            // Only log response size if we're using our internal response string
            if (!custom_callback) {
                HTTP_INFO_FMT("Posted {} bytes, received {} bytes from {}", data.size(), response.size(), url);
            } else {
                HTTP_INFO_FMT("Posted {} bytes to {} using custom callback", data.size(), url);
            }
            
            return response;
        } catch (const std::exception& e) {
            HTTP_ERROR_FMT("Exception during POST: {}", e.what());
            throw;
        }
    }
    
    // POST with lambda for header setup (used by existing code)
    template<typename F>
    std::string post(
        const std::string& url,
        const std::string& data,
        F header_setter,
        WriteCallback custom_callback = nullptr,
        void* custom_data = nullptr
    ) {
        try {
            // Create a CURL handle
            curl_handle handle;
            
            // Response string to store the result (if using default callback)
            std::string response;
            
            // Set URL
            curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
            
            // Set POST method and data
            curl_easy_setopt(handle.get(), CURLOPT_POST, 1L);
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, data.c_str());
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE, data.length());
            
            // Set write function and data
            if (custom_callback && custom_data) {
                curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, custom_callback);
                curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, custom_data);
            } else {
                curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, write_callback);
                curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response);
            }
            
            // Set timeouts
            curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, timeout_);
            curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, connect_timeout_);
            
            // Enable automatic redirect following
            curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 10L);
            
            // Set user agent
            curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "Rouen-HTTP/1.0");
            
            // Setup headers using the provided setter
            struct curl_slist* curl_headers = nullptr;
            auto header_appender = [&curl_headers](const std::string& header) {
                curl_headers = curl_slist_append(curl_headers, header.c_str());
            };
            
            // Call the header setter with our header_appender function
            header_setter(header_appender);
            
            if (curl_headers) {
                curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, curl_headers);
            }
            
            // Perform the request
            HTTP_INFO_FMT("Posting to URL: {}", url);
            CURLcode res = curl_easy_perform(handle.get());
            
            // Clean up headers if set
            if (curl_headers) {
                curl_slist_free_all(curl_headers);
            }
            
            // Check for errors
            if (res != CURLE_OK) {
                HTTP_ERROR_FMT("CURL POST request failed: {}", curl_easy_strerror(res));
                throw std::runtime_error(std::string("CURL POST request failed: ") + curl_easy_strerror(res));
            }
            
            // Check HTTP status code
            long http_code = 0;
            curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &http_code);
            
            if (http_code >= 400) {
                HTTP_ERROR_FMT("HTTP error: {} ({})", http_code, url);
                throw std::runtime_error("HTTP error " + std::to_string(http_code));
            }
            
            // Only log response size if we're using our internal response string
            if (!custom_callback) {
                HTTP_INFO_FMT("Posted {} bytes, received {} bytes from {}", data.size(), response.size(), url);
            } else {
                HTTP_INFO_FMT("Posted {} bytes to {} using custom callback", data.size(), url);
            }
            
            return response;
        } catch (const std::exception& e) {
            HTTP_ERROR_FMT("Exception during POST: {}", e.what());
            throw;
        }
    }
    
private:
    long timeout_;        // Request timeout in seconds
    long connect_timeout_; // Connection timeout in seconds
};

} // namespace http