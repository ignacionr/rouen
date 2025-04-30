#pragma once

#include <format>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

#include <curl/curl.h>

namespace mail {
    class imap_host {
    public:
        imap_host(const std::string& host, const std::string& user, const std::string& password): host_(host), user_(user), password_(password) {
            if (!host_.starts_with("imaps")) {
                throw std::invalid_argument("host must start with imaps");
            }
            if (!host_.ends_with("/")) {
                host_ += "/";
            }
            
            // Initialize CURL globally if needed
            static std::once_flag curl_init_flag;
            std::call_once(curl_init_flag, []() {
                curl_global_init(CURL_GLOBAL_DEFAULT);
            });
        }

        ~imap_host() { 
            disconnect();
        }
        
        std::vector<long long> get_mail_uids() {
            std::lock_guard lock(mutex_);
            ensure_connection();
            
            std::vector<long long> result;
            std::string single_buffer;
            auto url = std::format("{}{}/", host_, mailbox_);
            curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "UID SEARCH ALL");
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteToString);
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &single_buffer);
            auto res = curl_easy_perform(curl_);
            if(res != CURLE_OK) {
                handle_curl_error("get mails failed", res);
            }
            // the response will look like * SEARCH 1 2 3 4 5
            if (!single_buffer.starts_with("* SEARCH") || !single_buffer.ends_with("\r\n")) {
                throw std::runtime_error(std::format("get mails failed: {}", single_buffer));
            }
            std::string_view view(single_buffer.data() + 8, single_buffer.size() - 10); // skip * SEARCH and \r\n
            if (!view.empty()) { // skip empty results
                size_t start = 1;
                for (bool done = false; !done;) {
                    auto end = view.find(" ", start);
                    if (end == std::string_view::npos) {
                        end = view.size();
                        done = true;
                    }
                    result.push_back(std::stoll(std::string(view.substr(start, end - start))));
                    start = end + 1;
                }
            }
            return result;
        }

        std::string get_mail_header(long long uid) {
            std::lock_guard lock(mutex_);
            ensure_connection();
            
            std::string single_buffer;
            auto url = std::format("{}{}/;UID={};SECTION=HEADER", host_, mailbox_, uid);
            curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, nullptr);
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteToString);
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &single_buffer);
            auto res = curl_easy_perform(curl_);
            if(res != CURLE_OK) {
                handle_curl_error("get mail header failed", res);
            }
            return single_buffer;
        }

        std::string get_mail_body(long long uid) {
            std::lock_guard lock(mutex_);
            ensure_connection();
            
            std::string single_buffer;
            auto url = std::format("{}{}/;UID={};SECTION=TEXT", host_, mailbox_, uid);
            curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, nullptr);
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteToString);
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &single_buffer);
            auto res = curl_easy_perform(curl_);
            if(res != CURLE_OK) {
                handle_curl_error("get mail body failed", res);
            }
            return single_buffer;
        }

        void list_mailboxes(auto callback) {
            std::lock_guard lock(mutex_);
            ensure_connection();
            
            curl_easy_setopt(curl_, CURLOPT_URL, host_.c_str());
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "LIST \"\" \"*\"");
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteToString);
            std::string response;
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
            auto res = curl_easy_perform(curl_);
            if(res != CURLE_OK) {
                handle_curl_error("list mailboxes failed", res);
            }
            
            if (response.empty() || !response.contains("LIST")) {
                throw std::runtime_error("Invalid response for mailbox listing");
            }
            
            std::string_view view(response);
            while (view.find("LIST") != std::string_view::npos) {
                auto start = view.find("\"") + 1;
                auto end = view.find("\"", start);
                // we got the delimiter, which we don't care about, now obtain the mailbox name
                start = view.find("\"", end + 1) + 1;
                end = view.find("\"", start);
                
                if (start < end && start != std::string::npos && end != std::string::npos) {
                    callback(view.substr(start, end - start));
                }
                
                if (end == std::string::npos || end + 1 >= view.size()) {
                    break;
                }
                
                view = view.substr(end + 1);
            }
        }

        void connect() {
            std::lock_guard lock(mutex_);
            
            // Clean up existing connection if any
            if (curl_ != nullptr) {
                curl_easy_cleanup(curl_);
                curl_ = nullptr;
            }
            
            // Initialize a new CURL handle
            curl_ = curl_easy_init();
            if (!curl_) {
                throw std::runtime_error("Failed to initialize CURL");
            }
            
            // Configure CURL with connection options
            curl_easy_setopt(curl_, CURLOPT_USERNAME, user_.c_str());
            curl_easy_setopt(curl_, CURLOPT_PASSWORD, password_.c_str());
            curl_easy_setopt(curl_, CURLOPT_URL, host_.c_str());
            
            // Set timeouts to prevent hanging on network issues
            curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 10L);  // 10 seconds connection timeout
            curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L);         // 30 seconds for operation timeout
            
            // Enable verbose output for better debugging (only in debug builds)
            #ifdef DEBUG
            curl_easy_setopt(curl_, CURLOPT_VERBOSE, 1L);
            #endif
            
            // Check for empty username/password
            if (user_.empty() || password_.empty()) {
                throw std::runtime_error("Username or password is empty");
            }
            
            // Test connection
            auto res = curl_easy_perform(curl_);
            if (res != CURLE_OK) {
                handle_curl_error("connect failed", res);
            }
            
            // Mark as connected
            is_connected_ = true;
        }

        void disconnect() {
            std::lock_guard lock(mutex_);
            if(curl_) {
                curl_easy_cleanup(curl_);
                curl_ = nullptr;
            }
            is_connected_ = false;
        }

        void delete_message(long long uid) 
        {
            std::lock_guard lock(mutex_);
            ensure_connection();
            
            // Construct the IMAP URL for the DELETE operation
            auto url = std::format("{}{}/;UID={}", host_, mailbox_, uid);
            curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    
            // First, mark the email as \Deleted
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, std::format("UID STORE {} +FLAGS (\\Deleted)", uid).c_str());
    
            // Prepare response handling
            std::string response;
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteToString);
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    
            // Execute the request
            auto res = curl_easy_perform(curl_);
            if (res != CURLE_OK) {
                handle_curl_error("Delete message failed", res);
            }
    
            // Now expunge the mailbox to permanently remove deleted messages
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "EXPUNGE");
    
            res = curl_easy_perform(curl_);
            if (res != CURLE_OK) {
                handle_curl_error("Expunge failed", res);
            }
        }
        
        void move_message(long long uid, const std::string& target_mailbox) {
            // Check if target mailbox exists, create if it doesn't
            bool mailbox_exists = false;
            try {
                list_mailboxes([&mailbox_exists, &target_mailbox](std::string_view mailbox) {
                    if (mailbox == target_mailbox) {
                        mailbox_exists = true;
                    }
                });
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("Failed to check mailboxes: {}", e.what()));
            }
            
            std::lock_guard lock(mutex_);
            ensure_connection();
            
            if (!mailbox_exists) {
                // Create the target mailbox if it doesn't exist
                auto url = host_;
                curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
                std::string cmd = std::format("CREATE {}", target_mailbox);
                curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, cmd.c_str());
                
                std::string response;
                curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteToString);
                curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
                
                auto res = curl_easy_perform(curl_);
                if (res != CURLE_OK) {
                    handle_curl_error("Create mailbox failed", res);
                }
            }

            // Construct the IMAP URL for the COPY operation
            auto url = std::format("{}{}/;UID={}", host_, mailbox_, uid);
            curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
            
            // Copy the message to the target mailbox
            std::string cmd = std::format("UID COPY {} {}", uid, target_mailbox);
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, cmd.c_str());
            
            // Prepare response handling
            std::string response;
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteToString);
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
            
            // Execute the request
            auto res = curl_easy_perform(curl_);
            if (res != CURLE_OK) {
                handle_curl_error("Move message failed", res);
            }
            
            // If copy successful, delete the original message
            // Mark the email as \Deleted
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, std::format("UID STORE {} +FLAGS (\\Deleted)", uid).c_str());
            
            res = curl_easy_perform(curl_);
            if (res != CURLE_OK) {
                handle_curl_error("Mark for deletion failed", res);
            }
            
            // Now expunge the mailbox to permanently remove deleted messages
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "EXPUNGE");
            
            res = curl_easy_perform(curl_);
            if (res != CURLE_OK) {
                handle_curl_error("Expunge failed", res);
            }
        }
        
        void select_mailbox(const std::string_view mailbox) {
            std::lock_guard lock(mutex_);
            ensure_connection();
            
            mailbox_ = mailbox;
            curl_easy_setopt(curl_, CURLOPT_URL, host_.c_str());
            std::string cmd = std::format("SELECT {}", mailbox_);
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, cmd.c_str());
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteToString);
            std::string response;
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
            auto res = curl_easy_perform(curl_);
            if(res != CURLE_OK) {
                handle_curl_error("select mailbox failed", res);
            }
            // check if the mailbox is selected
            if (!response.contains("\r\n* OK")) {
                throw std::runtime_error(std::format("select mailbox failed: {}", response));
            }
        }
        
        // Check if currently connected
        bool is_connected() const {
            std::lock_guard lock(mutex_);
            return is_connected_ && curl_ != nullptr;
        }
        
    private:
        // Helper to ensure connection is active before performing operations
        void ensure_connection() {
            if (!is_connected_ || curl_ == nullptr) {
                connect();
            }
        }
        
        // Helper to handle CURL errors with detailed information
        void handle_curl_error(const std::string& operation, CURLcode res) {
            std::string error_msg = std::format("{}: {} ({})", 
                operation, 
                curl_easy_strerror(res), 
                static_cast<int>(res));
                
            // Get more detailed error information if available
            char error_buffer[CURL_ERROR_SIZE] = {0};
            curl_easy_getinfo(curl_, CURLINFO_PRIVATE, &error_buffer);
            if (error_buffer[0] != '\0') {
                error_msg += std::format(" - {}", error_buffer);
            }
            
            // Check if this is a network error that might benefit from reconnection
            if (res == CURLE_COULDNT_CONNECT || 
                res == CURLE_OPERATION_TIMEDOUT || 
                res == CURLE_SSL_CONNECT_ERROR) {
                // Reset connection state for next attempt
                is_connected_ = false;
            }
            
            throw std::runtime_error(error_msg);
        }
        
        static size_t WriteToVector(void* contents, size_t size, size_t nmemb, std::vector<std::string>* mails) {
            mails->emplace_back(static_cast<char*>(contents), size * nmemb);
            return size * nmemb;
        }
        
        static size_t WriteToString(void* contents, size_t size, size_t nmemb, std::string* str) {
            str->append(static_cast<char*>(contents), size * nmemb);
            return size * nmemb;
        }

        std::string host_;
        std::string user_;
        std::string password_;
        CURL* curl_ {};
        std::string mailbox_ {"INBOX"};
        bool is_connected_ {false};

        mutable std::mutex mutex_;
    };
}