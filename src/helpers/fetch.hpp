#pragma once

#include <format>
#include <functional>
#include <stdexcept>
#include <string>

#include <curl/curl.h>

namespace http {
    struct fetch {
        using header_setter_t = std::function<void(std::string const&)>;
        using header_client_t = std::function<void(header_setter_t)>;
        // typedef for the shape of the fwrite function
        typedef size_t (*write_callback_t)(void *, size_t, size_t, void *);

        fetch(long timeout = 30): timeout_{timeout} {
            curl_global_init(CURL_GLOBAL_ALL);
        }

        std::string operator()(std::string const &url, 
            header_client_t header_client = {},
            write_callback_t write_callback = fetch::write_string,
            void *write_data = nullptr) const
        {
            CURL *curl = curl_easy_init();
            if (curl) {
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "beat-o-graph/1.0");
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_);
                curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
                char *pproxy_str = nullptr;
                size_t len = 0;
                #if defined(_WIN32) || defined(_WIN64)
                if (!_dupenv_s(&pproxy_str, &len, "HTTP_PROXY") && pproxy_str != nullptr) {
                    curl_easy_setopt(curl, CURLOPT_PROXY, pproxy_str);
                }
                #endif
                if (header_client) {
                    struct curl_slist *headers = nullptr;
                    auto setheader = [&headers](std::string const& header_value) {
                        headers = curl_slist_append(headers, header_value.c_str());
                    };

                    header_client(setheader);
                    
                    if (headers) {
                        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                    }
                }
                std::string response;
                if (write_data) {
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, write_data);
                }
                else if (write_callback == fetch::write_string) {
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
                }
                auto cr = curl_easy_perform(curl);
                if (cr != CURLE_OK) {
                    curl_easy_cleanup(curl);
                    throw std::runtime_error(std::format("Error: {}", curl_easy_strerror(cr)));
                }
                // obtain the response code
                long response_code;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                if (response_code != 200) {
                    throw std::runtime_error(std::format("Error: HTTP response code {}", response_code));
                }
                curl_easy_cleanup(curl);
                return response;
            }
            throw std::runtime_error("Error: curl_easy_init failed.");
        }

        std::string post(
            std::string const &url, 
            std::string const &data, 
            header_client_t header_client) const {
            CURL *curl = curl_easy_init();
            if (curl) {
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "beat-o-graph/1.0");
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_);
                curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.size());
                char *pproxy_str = nullptr;
                size_t len = 0;
                #if defined(_WIN32) || defined(_WIN64)
                if (!_dupenv_s(&pproxy_str, &len, "HTTP_PROXY") && pproxy_str != nullptr) {
                    curl_easy_setopt(curl, CURLOPT_PROXY, pproxy_str);
                }
                #endif // defined(_WIN32) || defined(_WIN64)
                if (header_client) {
                    struct curl_slist *headers = nullptr;
                    auto setheader = [&headers](std::string const& header_value) {
                        headers = curl_slist_append(headers, header_value.c_str());
                    };

                    header_client(setheader);
                    
                    if (headers) {
                        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                    }
                }
                std::string response;
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch::write_string);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
                auto cr = curl_easy_perform(curl);
                if (cr != CURLE_OK) {
                    curl_easy_cleanup(curl);
                    throw std::runtime_error(std::format("Error: {}", curl_easy_strerror(cr)));
                }
                // obtain the response code
                long response_code;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                if (response_code != 200) {
                    throw std::runtime_error(std::format("Error: HTTP response code {}", response_code));
                }
                curl_easy_cleanup(curl);
                return response;
            }
            throw std::runtime_error("Error: curl_easy_init failed.");
        }
    private:
        static size_t write_string(void *ptr, size_t size, size_t nmemb, void *data) {
            auto *pdata = static_cast<std::string *>(data);
            pdata->append(static_cast<char *>(ptr), size * nmemb);
            return size * nmemb;
        }
        long timeout_;
    };
}