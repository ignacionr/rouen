#pragma once

#include <string>
#include <vector>
#include <regex>
#include <string_view>
#include <algorithm>

namespace media::html {
    
    struct extracted_media {
        std::string url;
        std::string type; // "video", "audio", or "unknown"
        std::string format; // "mp4", "webm", "m3u8", etc.
        
        extracted_media(std::string_view url_val, std::string_view type_val, std::string_view format_val)
            : url(url_val), type(type_val), format(format_val) {}
    };
    
    /**
     * Extract video/audio URLs from HTML content
     * Searches for:
     * - iframe src attributes with video extensions
     * - video/audio tags with src attributes
     * - direct links to media files
     * - embedded YouTube/Vimeo URLs
     */
    std::vector<extracted_media> extract_media_urls(std::string_view html_content);
    
    /**
     * Determine if a URL likely points to playable media
     */
    bool is_media_url(std::string_view url);
    
    /**
     * Extract format from URL (mp4, webm, etc.)
     */
    std::string extract_format(std::string_view url);
    
    /**
     * Clean and normalize media URLs for playback
     */
    std::string normalize_media_url(std::string_view url);
}

// --- Implementation ---

namespace media::html {
    
    inline std::vector<extracted_media> extract_media_urls(std::string_view html_content) {
        std::vector<extracted_media> results;
        std::string content_str(html_content);
        
        // Regular expressions for different media extraction patterns
        static const std::vector<std::pair<std::regex, std::string>> patterns = {
            // iframe src with video extensions
            {std::regex(R"(<iframe[^>]+src\s*=\s*[\"']([^\"']*\.(?:mp4|webm|avi|mov|mkv|m4v|3gp|flv)[^\"']*)[\"'][^>]*>)", 
                       std::regex_constants::icase), "video"},
            
            // video tag src
            {std::regex(R"(<video[^>]+src\s*=\s*[\"']([^\"']+)[\"'][^>]*>)", 
                       std::regex_constants::icase), "video"},
            
            // source tag within video with src
            {std::regex(R"(<source[^>]+src\s*=\s*[\"']([^\"']+)[\"'][^>]*type\s*=\s*[\"']video/[^\"']*[\"'][^>]*>)", 
                       std::regex_constants::icase), "video"},
            
            // audio tag src
            {std::regex(R"(<audio[^>]+src\s*=\s*[\"']([^\"']+)[\"'][^>]*>)", 
                       std::regex_constants::icase), "audio"},
            
            // source tag within audio with src
            {std::regex(R"(<source[^>]+src\s*=\s*[\"']([^\"']+)[\"'][^>]*type\s*=\s*[\"']audio/[^\"']*[\"'][^>]*>)", 
                       std::regex_constants::icase), "audio"},
            
            // YouTube embed URLs
            {std::regex(R"((?:youtube\.com/embed/|youtu\.be/)([a-zA-Z0-9_-]+))", 
                       std::regex_constants::icase), "video"},
            
            // Vimeo embed URLs
            {std::regex(R"(vimeo\.com/(?:video/)?(\d+))", 
                       std::regex_constants::icase), "video"},
            
            // Direct media file links
            {std::regex(R"(https?://[^\s<>"']+\.(?:mp4|webm|avi|mov|mkv|m4v|3gp|flv|mp3|wav|ogg|aac|m4a|wma)(?:\?[^\s<>"']*)?)", 
                       std::regex_constants::icase), "unknown"}
        };
        
        for (const auto& [pattern, media_type] : patterns) {
            std::sregex_iterator iter(content_str.begin(), content_str.end(), pattern);
            std::sregex_iterator end;
            
            for (; iter != end; ++iter) {
                std::string url = iter->str(1);
                
                // Handle YouTube URLs specially
                if (media_type == "video" && (iter->str(0).find("youtube") != std::string::npos || iter->str(0).find("youtu.be") != std::string::npos)) {
                    url = "https://www.youtube.com/watch?v=" + iter->str(1);
                }
                
                // Handle Vimeo URLs
                if (media_type == "video" && iter->str(0).find("vimeo") != std::string::npos) {
                    url = "https://vimeo.com/" + iter->str(1);
                }
                
                // Normalize the URL
                url = normalize_media_url(url);
                
                // Determine format
                std::string format = extract_format(url);
                
                // Avoid duplicates
                bool duplicate = false;
                for (const auto& existing : results) {
                    if (existing.url == url) {
                        duplicate = true;
                        break;
                    }
                }
                
                if (!duplicate && !url.empty()) {
                    results.emplace_back(url, media_type, format);
                }
            }
        }
        
        return results;
    }
    
    inline bool is_media_url(std::string_view url) {
        // Check for common media file extensions
        static const std::vector<std::string> media_extensions = {
            ".mp4", ".webm", ".avi", ".mov", ".mkv", ".m4v", ".3gp", ".flv",
            ".mp3", ".wav", ".ogg", ".aac", ".m4a", ".wma", ".m3u8", ".ts"
        };
        
        std::string url_lower(url);
        std::transform(url_lower.begin(), url_lower.end(), url_lower.begin(), ::tolower);
        
        for (const auto& ext : media_extensions) {
            if (url_lower.find(ext) != std::string::npos) {
                return true;
            }
        }
        
        // Check for known streaming services
        return url_lower.find("youtube.com") != std::string::npos ||
               url_lower.find("youtu.be") != std::string::npos ||
               url_lower.find("vimeo.com") != std::string::npos ||
               url_lower.find("twitch.tv") != std::string::npos;
    }
    
    inline std::string extract_format(std::string_view url) {
        std::string url_lower(url);
        std::transform(url_lower.begin(), url_lower.end(), url_lower.begin(), ::tolower);
        
        if (url_lower.find(".mp4") != std::string::npos) return "mp4";
        if (url_lower.find(".webm") != std::string::npos) return "webm";
        if (url_lower.find(".avi") != std::string::npos) return "avi";
        if (url_lower.find(".mov") != std::string::npos) return "mov";
        if (url_lower.find(".mkv") != std::string::npos) return "mkv";
        if (url_lower.find(".m4v") != std::string::npos) return "m4v";
        if (url_lower.find(".3gp") != std::string::npos) return "3gp";
        if (url_lower.find(".flv") != std::string::npos) return "flv";
        if (url_lower.find(".mp3") != std::string::npos) return "mp3";
        if (url_lower.find(".wav") != std::string::npos) return "wav";
        if (url_lower.find(".ogg") != std::string::npos) return "ogg";
        if (url_lower.find(".aac") != std::string::npos) return "aac";
        if (url_lower.find(".m4a") != std::string::npos) return "m4a";
        if (url_lower.find(".wma") != std::string::npos) return "wma";
        if (url_lower.find(".m3u8") != std::string::npos) return "hls";
        if (url_lower.find(".ts") != std::string::npos) return "ts";
        
        if (url_lower.find("youtube") != std::string::npos) return "youtube";
        if (url_lower.find("vimeo") != std::string::npos) return "vimeo";
        
        return "unknown";
    }
    
    inline std::string normalize_media_url(std::string_view url) {
        std::string normalized(url);
        
        // Remove surrounding whitespace
        normalized.erase(0, normalized.find_first_not_of(" \t\n\r"));
        normalized.erase(normalized.find_last_not_of(" \t\n\r") + 1);
        
        // Handle relative URLs (convert to absolute if needed)
        if (normalized.starts_with("//")) {
            normalized = "https:" + normalized;
        }
        
        // HTML entity decoding for common cases
        static const std::vector<std::pair<std::string, std::string>> entities = {
            {"&amp;", "&"},
            {"&quot;", "\""},
            {"&#39;", "'"},
            {"&lt;", "<"},
            {"&gt;", ">"}
        };
        
        for (const auto& [entity, replacement] : entities) {
            size_t pos = 0;
            while ((pos = normalized.find(entity, pos)) != std::string::npos) {
                normalized.replace(pos, entity.length(), replacement);
                pos += replacement.length();
            }
        }
        
        return normalized;
    }
}
