#pragma once

#include <algorithm>
#include <string>
#include <string_view>

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
};

} // namespace helpers