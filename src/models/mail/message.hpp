#pragma once

#include <chrono>
#include <set>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <regex>
#include <cctype>
#include <map>
#include <nlohmann/json.hpp>

namespace mail {
    class message {
    public:
        message(long long uid, std::string_view header) : uid_{uid}, header_{std::string{header}} {
            title_ = decode_header(get_header_field(header, "Subject"));
            from_ = decode_header(get_header_field(header, "From"));
            auto date_str = get_header_field(header, "Date");
            std::tm tm = {};
            std::istringstream ss{date_str};
            ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M %z");
            date_ = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }

        static std::string get_header_field(std::string_view header, std::string_view field) {
            auto marker = std::format("\r\n{}: ", field);
            auto start{header.find(marker)};
            if (start == std::string_view::npos) {
                return {};
            }
            start += marker.size();
            auto end = header.find("\r\n", start);
            return std::string{header.substr(start, end - start)};
        }

        // Decode encoded email header (RFC 2047)
        static std::string decode_header(const std::string& header) {
            if (header.empty()) {
                return header;
            }

            // Regex to match encoded words in headers
            // Format: =?charset?encoding?encoded-text?=
            std::regex encoded_word_regex("=\\?([^?]+)\\?([qQbB])\\?([^?]*)\\?=");
            
            std::string result = header;
            std::smatch match;
            std::string::const_iterator search_start(header.cbegin());
            
            std::vector<std::pair<std::size_t, std::size_t>> positions;
            std::vector<std::string> replacements;
            
            // Find all encoded words
            while (std::regex_search(search_start, header.cend(), match, encoded_word_regex)) {
                std::string charset = match[1];
                char encoding = std::toupper(match[2].str()[0]);
                std::string encoded_text = match[3];
                
                std::string decoded;
                if (encoding == 'Q') {
                    decoded = decode_quoted_printable(encoded_text);
                } else if (encoding == 'B') {
                    decoded = decode_base64(encoded_text);
                }
                
                // Store position and replacement
                auto pos = std::distance(header.cbegin(), match[0].first);
                auto len = match[0].length();
                positions.push_back({pos, len});
                replacements.push_back(decoded);
                
                search_start = match.suffix().first;
            }
            
            // Apply replacements from right to left to maintain correct positions
            for (int i = static_cast<int>(positions.size()) - 1; i >= 0; --i) {
                auto [pos, len] = positions[i];
                result.replace(pos, len, replacements[i]);
            }
            
            return result;
        }
        
        // Decode quoted-printable text
        static std::string decode_quoted_printable(const std::string& input) {
            std::string result;
            result.reserve(input.size());
            
            for (size_t i = 0; i < input.size(); i++) {
                if (input[i] == '=') {
                    // Handle escaped hex values
                    if (i + 2 < input.size()) {
                        std::string hex = input.substr(i + 1, 2);
                        try {
                            int value = std::stoi(hex, nullptr, 16);
                            result.push_back(static_cast<char>(value));
                            i += 2;
                        } catch (...) {
                            // Invalid hex, just add the equals sign
                            result.push_back('=');
                        }
                    } else {
                        // Not enough characters left, just add equals
                        result.push_back('=');
                    }
                } else if (input[i] == '_') {
                    // Underscore in Q-encoding represents a space
                    result.push_back(' ');
                } else {
                    result.push_back(input[i]);
                }
            }
            
            return result;
        }
        
        // Decode base64 text
        static std::string decode_base64(const std::string& input) {
            static const std::string base64_chars = 
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                
            std::string result;
            std::vector<int> char_array_4(4, 0);
            std::vector<int> char_array_3(3, 0);
            int i = 0, j = 0;
            int in_len = input.size();
            unsigned char c = 0;
            
            while (in_len-- && (input[i] != '=') && is_base64(input[i])) {
                c = input[i++];
                c = base64_chars.find(c);
                
                char_array_4[j++] = c;
                if (j == 4) {
                    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
                    
                    for (j = 0; j < 3; j++) {
                        result += char_array_3[j];
                    }
                    j = 0;
                }
            }
            
            if (j) {
                size_t left = j;
                for (size_t i = left; i < 4; i++) {
                    char_array_4[i] = 0;
                }
                
                char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
                
                for (size_t i = 0; i < left - 1; i++) {
                    result += char_array_3[i];
                }
            }
            
            return result;
        }
        
        // Check if a character is a valid base64 character
        static bool is_base64(unsigned char c) {
            return (isalnum(c) || (c == '+') || (c == '/'));
        }

        void set_metadata(const std::string& metadata_json) {
            metadata_ = metadata_json;
            
            try {
                // Parse JSON metadata
                auto metadata = nlohmann::json::parse(metadata_);
                
                if (metadata.contains("urgency")) {
                    urgency_ = metadata["urgency"].get<int>();
                }
                
                if (metadata.contains("category")) {
                    category_ = metadata["category"].get<std::string>();
                }
                
                if (metadata.contains("summary")) {
                    summary_ = metadata["summary"].get<std::string>();
                }
                
                if (metadata.contains("tags") && metadata["tags"].is_array()) {
                    for (const auto& tag : metadata["tags"]) {
                        tags_.insert(tag.get<std::string>());
                    }
                }
                
                // Parse action links if available
                if (metadata.contains("action_links") && metadata["action_links"].is_object()) {
                    for (auto& [action, link] : metadata["action_links"].items()) {
                        if (link.is_string()) {
                            action_links_[action] = link.get<std::string>();
                        }
                    }
                }
            } catch (const std::exception& e) {
                // Handle parsing errors
                summary_ = "Error parsing metadata: " + std::string(e.what());
            }
        }

        std::string const &metadata() const { return metadata_; }
        std::string const &title() const { return title_; }
        std::string const &from() const { return from_; }
        std::string const &header() const { return header_; }
        std::chrono::system_clock::time_point date() const { return date_; }

        int urgency() const { return urgency_; }
        std::string const &category() const { return category_; }
        std::string const &summary() const { return summary_; }
        std::set<std::string> const &tags() const { return tags_; }
        const std::map<std::string, std::string>& action_links() const { return action_links_; }
        long long uid() const { return uid_; }

    private:
        long long uid_;
        std::string header_;
        std::string title_;
        std::string from_;
        std::string metadata_;
        int urgency_{0};
        std::string category_;
        std::string summary_;
        std::set<std::string> tags_;
        std::map<std::string, std::string> action_links_;
        std::chrono::system_clock::time_point date_;
    };
}