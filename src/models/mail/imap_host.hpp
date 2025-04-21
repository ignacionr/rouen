#pragma once

#include <format>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

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
        }

        ~imap_host() { 
            disconnect();
        }
        
        std::vector<long long> get_mail_uids() {
            std::lock_guard lock(mutex_);
            std::vector<long long> result;
            std::string single_buffer;
            auto url = std::format("{}{}/", host_, mailbox_);
            curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "UID SEARCH ALL");
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteToString);
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &single_buffer);
            auto res = curl_easy_perform(curl_);
            if(res != CURLE_OK) {
                throw std::runtime_error("get mails failed");
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
            std::string single_buffer;
            auto url = std::format("{}{}/;UID={};SECTION=HEADER", host_, mailbox_, uid);
            curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, nullptr);
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteToString);
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &single_buffer);
            auto res = curl_easy_perform(curl_);
            if(res != CURLE_OK) {
                throw std::runtime_error("get mail header failed");
            }
            return single_buffer;
        }

        std::string get_mail_body(long long uid) {
            std::lock_guard lock(mutex_);
            std::string single_buffer;
            auto url = std::format("{}{}/;UID={};SECTION=TEXT", host_, mailbox_, uid);
            curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, nullptr);
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteToString);
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &single_buffer);
            auto res = curl_easy_perform(curl_);
            if(res != CURLE_OK) {
                throw std::runtime_error("get mail body failed");
            }
            return single_buffer;
        }

        void list_mailboxes(auto callback) {
            std::lock_guard lock(mutex_);
            curl_easy_setopt(curl_, CURLOPT_URL, host_.c_str());
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "LIST \"\" \"*\"");
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteToString);
            std::string response;
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
            auto res = curl_easy_perform(curl_);
            if(res != CURLE_OK) {
                throw std::runtime_error("list mailboxes failed");
            }
            std::string_view view(response);
            while (view.find("LIST") != std::string_view::npos) {
                auto start = view.find("\"") + 1;
                auto end = view.find("\"", start);
                // we got the delimiter, which we don't care about, now obtain the mailbox name
                start = view.find("\"", end + 1) + 1;
                end = view.find("\"", start);
                callback(view.substr(start, end - start));
                view = view.substr(end + 1);
            }
        }

        void connect() {
            std::lock_guard lock(mutex_);
            if (curl_ == nullptr) {
                curl_ = curl_easy_init();
                if(curl_) {
                    curl_easy_setopt(curl_, CURLOPT_USERNAME, user_.c_str());
                    curl_easy_setopt(curl_, CURLOPT_PASSWORD, password_.c_str());
                    curl_easy_setopt(curl_, CURLOPT_URL, host_.c_str());
                    auto res = curl_easy_perform(curl_);
                    if(res != CURLE_OK) {
                        throw std::runtime_error("connect failed");
                    }
                }
            }
        }

        void disconnect() {
            std::lock_guard lock(mutex_);
            if(curl_) {
                curl_easy_cleanup(curl_);
                curl_ = nullptr;
            }
        }

        void delete_message(long long uid) 
        {
            std::lock_guard lock(mutex_);
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
                throw std::runtime_error(std::format("Delete message failed: {}", curl_easy_strerror(res)));
            }
    
            // Now expunge the mailbox to permanently remove deleted messages
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "EXPUNGE");
    
            res = curl_easy_perform(curl_);
            if (res != CURLE_OK) {
                throw std::runtime_error(std::format("Expunge failed: {}", curl_easy_strerror(res)));
            }
        }
        
        void select_mailbox(const std::string_view mailbox) {
            std::lock_guard lock(mutex_);
            mailbox_ = mailbox;
            curl_easy_setopt(curl_, CURLOPT_URL, host_.c_str());
            std::string cmd = std::format("SELECT {}", mailbox_);
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, cmd.c_str());
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteToString);
            std::string response;
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
            auto res = curl_easy_perform(curl_);
            if(res != CURLE_OK) {
                throw std::runtime_error("select mailbox failed");
            }
            // check if the mailbox is selected
            if (!response.contains("\r\n* OK")) {
                throw std::runtime_error("select mailbox failed");
            }
        }
    private:
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

        std::mutex mutex_;
    };
}