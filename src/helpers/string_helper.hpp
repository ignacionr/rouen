#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <sstream>
#include <iomanip>

namespace helpers {

/**
 * String utility functions for the Rouen application
 */
class StringHelper {
public:
    /**
     * Converts a string to lowercase (in-place).
     * 
     * @param str String to convert to lowercase
     * @return Reference to the modified string
     */
    static std::string& to_lower_inplace(std::string& str) {
        std::transform(str.begin(), str.end(), str.begin(), 
                      [](unsigned char c) { return std::tolower(c); });
        return str;
    }
    
    /**
     * Converts a string to lowercase (returning a new string).
     * 
     * @param str String to convert to lowercase
     * @return A new lowercase string
     */
    static std::string to_lower(std::string_view str) {
        std::string result(str);
        return to_lower_inplace(result);
    }
    
    /**
     * Checks if a string contains another string (case-insensitive).
     * 
     * @param haystack The string to search in
     * @param needle The string to search for
     * @return true if the haystack contains the needle (ignoring case), false otherwise
     */
    static bool contains_case_insensitive(std::string_view haystack, std::string_view needle) {
        if (needle.empty()) return true;
        if (haystack.empty()) return false;
        
        // Convert both strings to lowercase for comparison
        return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
    }
    
    /**
     * Checks if a string starts with another string (case-insensitive).
     * 
     * @param str The string to check
     * @param prefix The prefix to look for
     * @return true if the string starts with the prefix (ignoring case), false otherwise
     */
    static bool starts_with_case_insensitive(std::string_view str, std::string_view prefix) {
        if (str.size() < prefix.size()) {
            return false;
        }
        
        // Use substring view for comparison - no memory allocation
        auto str_start = str.substr(0, prefix.size());
        return to_lower(str_start) == to_lower(prefix);
    }
    
    /**
     * Checks if a string ends with another string (case-insensitive).
     * 
     * @param str The string to check
     * @param suffix The suffix to look for
     * @return true if the string ends with the suffix (ignoring case), false otherwise
     */
    static bool ends_with_case_insensitive(std::string_view str, std::string_view suffix) {
        if (str.size() < suffix.size()) {
            return false;
        }
        
        // Use substring view for comparison - no memory allocation
        auto str_end = str.substr(str.size() - suffix.size());
        return to_lower(str_end) == to_lower(suffix);
    }

    /**
     * URL encodes a string.
     * 
     * @param s The string to encode
     * @return The URL-encoded string
     */
    static std::string url_encode(std::string_view s) {
        std::ostringstream oss;
        oss.fill('0');
        oss << std::hex << std::uppercase;
        for (char ch : s) {
            const unsigned char c = static_cast<unsigned char>(ch);
            if ((c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~') {
                oss << static_cast<char>(c);
            } else if (c == ' ') {
                oss << "%20";
            } else {
                oss << '%' << std::setw(2) << int(c);
                oss << std::setw(0);
            }
        }
        return oss.str();
    }

    /**
     * Strips HTML tags and decodes common HTML entities from a string.
     * Replaces multiple whitespace characters with a single space.
     * 
     * @param html The HTML string to clean
     * @return Cleaned plain text
     */
    static std::string strip_html_tags(std::string_view html) {
        std::string tag_stripped;
        tag_stripped.reserve(html.size());
        
        // 1. Strip HTML tags
        bool in_tag = false;
        for (char c : html) {
            if (c == '<') {
                in_tag = true;
            } else if (c == '>') {
                in_tag = false;
            } else if (!in_tag) {
                tag_stripped.push_back(c);
            }
        }
        
        // 2. Decode HTML entities from the stripped text
        std::string decoded;
        decoded.reserve(tag_stripped.size());
        for (size_t i = 0; i < tag_stripped.size(); ++i) {
            if (tag_stripped[i] == '&') {
                if (tag_stripped.substr(i, 4) == "&lt;") {
                    decoded.push_back('<');
                    i += 3;
                } else if (tag_stripped.substr(i, 4) == "&gt;") {
                    decoded.push_back('>');
                    i += 3;
                } else if (tag_stripped.substr(i, 5) == "&amp;") {
                    decoded.push_back('&');
                    i += 4;
                } else if (tag_stripped.substr(i, 6) == "&quot;") {
                    decoded.push_back('"');
                    i += 5;
                } else if (tag_stripped.substr(i, 6) == "&apos;") {
                    decoded.push_back('\'');
                    i += 5;
                } else if (tag_stripped.substr(i, 5) == "&#39;") {
                    decoded.push_back('\'');
                    i += 4;
                } else if (tag_stripped.substr(i, 6) == "&nbsp;") {
                    decoded.push_back(' ');
                    i += 5;
                } else {
                    decoded.push_back(tag_stripped[i]);
                }
            } else {
                decoded.push_back(tag_stripped[i]);
            }
        }
        
        // 3. Normalize whitespace (collapse multiple spaces/newlines/tabs into a single space)
        std::string normalized;
        normalized.reserve(decoded.size());
        bool last_was_ws = false;
        
        // Find first non-whitespace
        size_t start = 0;
        while (start < decoded.size() && std::isspace(static_cast<unsigned char>(decoded[start]))) {
            start++;
        }
        
        // Find last non-whitespace
        size_t end = decoded.size();
        while (end > start && std::isspace(static_cast<unsigned char>(decoded[end - 1]))) {
            end--;
        }
        
        for (size_t i = start; i < end; ++i) {
            char c = decoded[i];
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!last_was_ws) {
                    normalized.push_back(' ');
                    last_was_ws = true;
                }
            } else {
                normalized.push_back(c);
                last_was_ws = false;
            }
        }
        
        return normalized;
    }
};

} // namespace helpers
