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
    
    // SSL verification options for corporate environments
    struct SSLOptions {
        bool verify_peer = true;        // Verify peer certificate
        bool verify_host = true;        // Verify hostname in certificate
        bool check_revocation = true;   // Check certificate revocation (CRL/OCSP)
        std::string cipher_list = "ECDHE+AESGCM:ECDHE+CHACHA20:ECDHE+AES256:ECDHE+AES128:RSA+AESGCM:RSA+AES:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!SRP"; // Default secure but compatible cipher list
        
        // Factory method for relaxed SSL settings (useful for corporate environments and Atlassian Cloud)
        static SSLOptions relaxed() {
            SSLOptions opts;
            opts.verify_peer = true;      // Still verify the certificate chain
            opts.verify_host = true;      // Still verify hostname matches
            opts.check_revocation = false; // Skip revocation checks that often fail in corporate environments
            // Use a very permissive cipher list optimized for Atlassian Cloud and corporate environments
            opts.cipher_list = "ECDHE+AESGCM:ECDHE+CHACHA20:ECDHE+AES256:ECDHE+AES128:DHE+AESGCM:DHE+AES256:DHE+AES128:RSA+AESGCM:RSA+AES256:RSA+AES128:AES256-GCM-SHA384:AES128-GCM-SHA256:AES256-SHA256:AES128-SHA256:AES256-SHA:AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!SRP";
            return opts;
        }
        
        // Factory method for maximum compatibility (for problematic servers)
        static SSLOptions compatible() {
            SSLOptions opts;
            opts.verify_peer = true;      // Still verify the certificate chain
            opts.verify_host = true;      // Still verify hostname matches
            opts.check_revocation = false; // Skip revocation checks
            // Maximum compatibility cipher list - includes older but still secure ciphers
            // This list is designed to work with services like Atlassian Cloud
            opts.cipher_list = "ALL:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!SRP:!LOW";
            return opts;
        }
        
        // Factory method for Atlassian Cloud specific compatibility
        static SSLOptions atlassian() {
            SSLOptions opts;
            opts.verify_peer = true;      // Still verify the certificate chain
            opts.verify_host = true;      // Still verify hostname matches
            opts.check_revocation = false; // Skip revocation checks
            // Ultra-permissive cipher list specifically for Atlassian Cloud
            // Allows most modern ciphers including those used by Atlassian's CDN
            opts.cipher_list = "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA:ECDHE-RSA-AES128-SHA:AES256-GCM-SHA384:AES128-GCM-SHA256:AES256-SHA256:AES128-SHA256:AES256-SHA:AES128-SHA:HIGH:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!SRP";
            return opts;
        }
        
        // Factory method for strict SSL settings (default)
        static SSLOptions strict() {
            return SSLOptions{}; // Uses default values with secure cipher list
        }
        
        // Factory method for completely insecure settings (use with caution)
        static SSLOptions insecure() {
            SSLOptions opts;
            opts.verify_peer = false;
            opts.verify_host = false;
            opts.check_revocation = false;
            // Use very permissive cipher list for maximum compatibility
            opts.cipher_list = "ALL:!aNULL:!eNULL:!LOW:!EXPORT:!SSLv2";
            return opts;
        }
    };
    
    // Constructor with default timeout
    fetch() : timeout_(30), connect_timeout_(10), ssl_options_(get_ssl_options_from_env()) {
        initialize_curl();
    }
    
    // Constructor with custom timeout
    explicit fetch(long timeout) : timeout_(timeout), connect_timeout_(timeout > 10 ? 10 : timeout), ssl_options_(get_ssl_options_from_env()) {
        initialize_curl();
        HTTP_INFO_FMT("Created fetch client with timeout: {}s", timeout);
    }
    
    // Constructor with custom timeout and SSL options
    fetch(long timeout, const SSLOptions& ssl_opts) : timeout_(timeout), connect_timeout_(timeout > 10 ? 10 : timeout), ssl_options_(ssl_opts) {
        initialize_curl();
        HTTP_INFO_FMT("Created fetch client with timeout: {}s and custom SSL options", timeout);
        log_ssl_options();
    }
    
    // Set SSL options after construction
    void set_ssl_options(const SSLOptions& opts) {
        ssl_options_ = opts;
        log_ssl_options();
    }
    
    // Get current SSL options
    const SSLOptions& get_ssl_options() const {
        return ssl_options_;
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
            
            // Configure SSL/TLS options
            configure_ssl_options(handle.get());
            
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
                handle_curl_error(res, url);
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
            
            // Configure SSL/TLS options
            configure_ssl_options(handle.get());
            
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
                handle_curl_error(res, url);
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
            
            // Configure SSL/TLS options
            configure_ssl_options(handle.get());
            
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
                handle_curl_error(res, url);
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
            
            // Configure SSL/TLS options
            configure_ssl_options(handle.get());
            
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
                handle_curl_error(res, url);
            }
            
            // Check HTTP status code
            long http_code = 0;
            curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &http_code);
            
            if (http_code >= 400) {
                HTTP_ERROR_FMT("HTTP error: {} ({})", http_code, url);
                
                // For specific error codes, log the response body which often contains useful information
                if (http_code == 429) {
                    HTTP_ERROR_FMT("Rate limit response body: {}", response.empty() ? "(empty)" : response);
                } else if (http_code >= 400 && http_code < 500) {
                    HTTP_DEBUG_FMT("Client error response body: {}", response.empty() ? "(empty)" : response.substr(0, 500));
                } else if (http_code >= 500) {
                    HTTP_DEBUG_FMT("Server error response body: {}", response.empty() ? "(empty)" : response.substr(0, 500));
                }
                
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
    
    // PUT request method
    std::string put(
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
            
            // Set write function and data
            if (custom_callback && custom_data) {
                curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, custom_callback);
                curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, custom_data);
            } else {
                curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, write_callback);
                curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response);
            }
            
            // Set PUT method and data
            curl_easy_setopt(handle.get(), CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, data.c_str());
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE, data.length());
            
            // Set timeouts
            curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, timeout_);
            curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, connect_timeout_);
            
            // Enable automatic redirect following
            curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 10L);
            
            // Set user agent
            curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "Rouen-HTTP/1.0");
            
            // Configure SSL/TLS options
            configure_ssl_options(handle.get());
            
            // Add custom headers if provided
            struct curl_slist* curl_headers = nullptr;
            if (!headers.empty()) {
                for (const auto& header : headers) {
                    curl_headers = curl_slist_append(curl_headers, header.c_str());
                }
                curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, curl_headers);
            }
            
            // Perform the request
            CURLcode res = curl_easy_perform(handle.get());
            
            // Clean up headers
            if (curl_headers) {
                curl_slist_free_all(curl_headers);
            }
            
            // Check for errors
            if (res != CURLE_OK) {
                handle_curl_error(res, url);
            }
            
            // Check HTTP response code
            long response_code;
            curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &response_code);
            if (response_code >= 400) {
                HTTP_ERROR_FMT("HTTP error: {} ({})", response_code, url);
                throw std::runtime_error("HTTP error " + std::to_string(response_code));
            }
            
            HTTP_INFO_FMT("Putting to URL: {}", url);
            
            // Log success
            if (!custom_callback) {
                HTTP_INFO_FMT("Put {} bytes, received {} bytes from {}", data.size(), response.size(), url);
            } else {
                HTTP_INFO_FMT("Put {} bytes to {} using custom callback", data.size(), url);
            }
            
            return response;
        } catch (const std::exception& e) {
            HTTP_ERROR_FMT("Exception during PUT: {}", e.what());
            throw;
        }
    }
    
private:
    long timeout_;        // Request timeout in seconds
    long connect_timeout_; // Connection timeout in seconds
    SSLOptions ssl_options_; // SSL/TLS configuration options
    
    // Initialize CURL globally
    void initialize_curl() {
        static bool curl_initialized = false;
        if (!curl_initialized) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curl_initialized = true;
            HTTP_INFO("CURL globally initialized");
        }
    }
    
    // Configure SSL/TLS options for a CURL handle
    void configure_ssl_options(CURL* handle) const {
        // Configure peer certificate verification
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, ssl_options_.verify_peer ? 1L : 0L);
        
        // Configure hostname verification
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, ssl_options_.verify_host ? 2L : 0L);
        
        // Configure certificate revocation checking
        if (!ssl_options_.check_revocation) {
            // Disable certificate revocation checks (CRL/OCSP)
            // This helps in corporate environments where revocation servers are not accessible
            #ifdef CURLOPT_SSL_OPTIONS
            curl_easy_setopt(handle, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);
            #endif
        }
        
        // Additional SSL options for better compatibility
        // Use system's CA bundle for certificate verification
        curl_easy_setopt(handle, CURLOPT_CAINFO, NULL);  // Use system default
        curl_easy_setopt(handle, CURLOPT_CAPATH, NULL);  // Use system default
        
        // Set SSL/TLS version - use TLS 1.2 as minimum for security
        curl_easy_setopt(handle, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
        
        // Enable ALPN (Application-Layer Protocol Negotiation) for HTTP/2 support
        // Note: NPN is deprecated since curl 7.86.0, ALPN is the modern replacement
        #ifdef CURLOPT_SSL_ENABLE_ALPN
        curl_easy_setopt(handle, CURLOPT_SSL_ENABLE_ALPN, 1L);
        #endif
        
        // Set cipher list for broad compatibility while maintaining security
        // Use the cipher list from SSL options which varies based on the SSL mode
        curl_easy_setopt(handle, CURLOPT_SSL_CIPHER_LIST, ssl_options_.cipher_list.c_str());
        
        // Additional options for corporate environments
        curl_easy_setopt(handle, CURLOPT_SSL_SESSIONID_CACHE, 1L);  // Enable SSL session reuse
        
        // Enable SSL false start for performance (if supported)
        #ifdef CURLOPT_SSL_FALSESTART
        curl_easy_setopt(handle, CURLOPT_SSL_FALSESTART, 1L);
        #endif
        
        // Log SSL configuration for debugging
        HTTP_DEBUG_FMT("SSL Config - Verify Peer: {}, Verify Host: {}, Check Revocation: {}", 
                      ssl_options_.verify_peer, ssl_options_.verify_host, ssl_options_.check_revocation);
        
        // Log SSL version and cipher info
        HTTP_DEBUG("SSL using TLS 1.2+, SNI enabled, system CA bundle");
    }
    
    // Get SSL options from environment variables
    SSLOptions get_ssl_options_from_env() const {
        SSLOptions opts;
        
        // Check for environment variables that control SSL behavior
        const char* ssl_verify_peer = std::getenv("ROUEN_SSL_VERIFY_PEER");
        const char* ssl_verify_host = std::getenv("ROUEN_SSL_VERIFY_HOST");
        const char* ssl_check_revocation = std::getenv("ROUEN_SSL_CHECK_REVOCATION");
        const char* ssl_mode = std::getenv("ROUEN_SSL_MODE");
        
    // Handle SSL mode presets
    if (ssl_mode) {
        std::string mode(ssl_mode);
        if (mode == "relaxed") {
            opts = SSLOptions::relaxed();
            HTTP_INFO("Using relaxed SSL mode - suitable for corporate environments");
        } else if (mode == "compatible") {
            opts = SSLOptions::compatible();
            HTTP_INFO("Using compatible SSL mode - maximum cipher compatibility");
        } else if (mode == "atlassian") {
            opts = SSLOptions::atlassian();
            HTTP_INFO("Using Atlassian SSL mode - optimized for Atlassian Cloud services");
        } else if (mode == "strict") {
            opts = SSLOptions::strict();
            HTTP_INFO("Using strict SSL mode - full certificate validation");
        } else if (mode == "insecure") {
            opts = SSLOptions::insecure();
            HTTP_WARN("Using insecure SSL mode - certificate validation disabled");
        }
    }
        
        // Override with specific environment variables if set
        if (ssl_verify_peer) {
            opts.verify_peer = (std::string(ssl_verify_peer) == "1" || std::string(ssl_verify_peer) == "true");
        }
        
        if (ssl_verify_host) {
            opts.verify_host = (std::string(ssl_verify_host) == "1" || std::string(ssl_verify_host) == "true");
        }
        
        if (ssl_check_revocation) {
            opts.check_revocation = (std::string(ssl_check_revocation) == "1" || std::string(ssl_check_revocation) == "true");
        }
        
        return opts;
    }
    
    // Log current SSL options for debugging
    void log_ssl_options() const {
        HTTP_INFO_FMT("SSL Options - Peer Verification: {}, Host Verification: {}, Revocation Checking: {}",
                     ssl_options_.verify_peer ? "enabled" : "disabled",
                     ssl_options_.verify_host ? "enabled" : "disabled", 
                     ssl_options_.check_revocation ? "enabled" : "disabled");
    }
    
    // Handle CURL errors with detailed SSL troubleshooting
    [[noreturn]] void handle_curl_error(CURLcode res, const std::string& url) const {
        std::string error_msg = curl_easy_strerror(res);
        
        // Provide more detailed error information for SSL issues
        if (res == CURLE_SSL_CONNECT_ERROR) {
            HTTP_ERROR_FMT("SSL connection failed to {}: {}", url, error_msg);
            HTTP_ERROR("Troubleshooting suggestions:");
            HTTP_ERROR("1. Try setting ROUEN_SSL_MODE=relaxed for corporate environments");
            HTTP_ERROR("2. Try setting ROUEN_SSL_MODE=insecure for testing (not secure!)");
            HTTP_ERROR("3. Check if the server requires specific SSL/TLS versions");
            HTTP_ERROR("4. Verify system time is correct (SSL certificates are time-sensitive)");
            HTTP_ERROR("5. Check if corporate firewall/proxy is interfering");
            
            throw std::runtime_error("SSL connection failed: " + error_msg + 
                                   " (Try setting ROUEN_SSL_MODE=relaxed for corporate networks)");
        } else if (res == CURLE_SSL_PEER_CERTIFICATE || res == CURLE_SSL_CACERT) {
            HTTP_ERROR_FMT("SSL certificate verification failed for {}: {}", url, error_msg);
            HTTP_ERROR("Certificate validation failed. Try ROUEN_SSL_MODE=relaxed to bypass certificate revocation checks");
            
            throw std::runtime_error("SSL certificate error: " + error_msg + 
                                   " (Try ROUEN_SSL_MODE=relaxed)");
        } else if (res == CURLE_SSL_CIPHER) {
            HTTP_ERROR_FMT("SSL cipher negotiation failed for {}: {}", url, error_msg);
            HTTP_ERROR("Server and client couldn't agree on SSL cipher. Try ROUEN_SSL_MODE=relaxed");
            
            throw std::runtime_error("SSL cipher error: " + error_msg);
        } else {
            HTTP_ERROR_FMT("CURL request failed for {}: {}", url, error_msg);
            throw std::runtime_error("CURL request failed: " + error_msg);
        }
    }
};

} // namespace http
