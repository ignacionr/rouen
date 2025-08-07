/**
 * Debug RT Media Detection
 * Purpose: Test our media detection against real RT RSS content to identify issues
 */

#include <iostream>
#include <string>
#include <vector>
#include <curl/curl.h>

// Include the actual source files we want to test
#include "src/helpers/html_media_extractor.hpp"
#include "src/models/rss/feed.hpp"
#include "src/models/rss/feed_item.hpp"

// Callback for CURL to write data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data) {
    size_t total_size = size * nmemb;
    data->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Fetch RSS content using CURL
std::string fetchRSSContent(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string response;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if(res != CURLE_OK) {
            std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
            return "";
        }
    }
    
    return response;
}

int main() {
    std::cout << "=== Debug RT Media Detection ===" << std::endl;
    
    // Fetch real RT RSS content
    std::cout << "1. Fetching RT RSS content..." << std::endl;
    std::string rss_content = fetchRSSContent("https://www.rt.com/rss/");
    
    if (rss_content.empty()) {
        std::cerr << "Failed to fetch RSS content" << std::endl;
        return 1;
    }
    
    std::cout << "   Downloaded " << rss_content.size() << " bytes" << std::endl;
    
    // Parse with our RSS parser
    std::cout << "\n2. Parsing with our RSS parser..." << std::endl;
    media::rss::feed feed_parser;
    feed_parser(rss_content);
    
    std::cout << "   Feed title: " << feed_parser.feed_title << std::endl;
    std::cout << "   Total items parsed: " << feed_parser.items.size() << std::endl;
    
    // Count items with media
    int items_with_media = 0;
    int total_extracted_urls = 0;
    
    for (const auto& item : feed_parser.items) {
        if (!item.extracted_media_urls.empty()) {
            items_with_media++;
            total_extracted_urls += item.extracted_media_urls.size();
        }
    }
    
    std::cout << "   Items with extracted media: " << items_with_media << std::endl;
    std::cout << "   Total extracted media URLs: " << total_extracted_urls << std::endl;
    
    // Debug first few items that should have video
    std::cout << "\n3. Detailed analysis of first 5 items:" << std::endl;
    
    for (size_t i = 0; i < std::min(5UL, feed_parser.items.size()); i++) {
        const auto& item = feed_parser.items[i];
        
        std::cout << "\n   Item " << (i+1) << ":" << std::endl;
        std::cout << "     Title: " << item.title.substr(0, 50) << "..." << std::endl;
        std::cout << "     Has description: " << (!item.description.empty() ? "YES" : "NO") << std::endl;
        std::cout << "     Description length: " << item.description.size() << " chars" << std::endl;
        std::cout << "     Extracted media URLs: " << item.extracted_media_urls.size() << std::endl;
        
        // Search for iframe in description manually
        size_t iframe_pos = item.description.find("<iframe");
        std::cout << "     Contains '<iframe': " << (iframe_pos != std::string::npos ? "YES" : "NO") << std::endl;
        
        if (iframe_pos != std::string::npos) {
            // Extract the iframe for analysis
            size_t iframe_end = item.description.find("</iframe>", iframe_pos);
            if (iframe_end != std::string::npos) {
                std::string iframe_content = item.description.substr(iframe_pos, iframe_end - iframe_pos + 9);
                std::cout << "     Iframe content (first 100 chars): " << iframe_content.substr(0, 100) << "..." << std::endl;
                
                // Check if it contains .mp4
                if (iframe_content.find(".mp4") != std::string::npos) {
                    std::cout << "     Iframe contains .mp4: YES" << std::endl;
                } else {
                    std::cout << "     Iframe contains .mp4: NO" << std::endl;
                }
            }
        }
        
        // Test direct HTML extraction on this content
        if (!item.description.empty()) {
            auto direct_extracted = media::html::extract_media_urls(item.description);
            std::cout << "     Direct HTML extraction results: " << direct_extracted.size() << " URLs" << std::endl;
            
            for (const auto& url : direct_extracted) {
                std::cout << "       - " << url.type << ": " << url.url.substr(0, 60) << "..." << std::endl;
            }
        }
    }
    
    // Show one complete content:encoded section that should have video
    std::cout << "\n4. Looking for content with .mp4 URLs..." << std::endl;
    
    for (size_t i = 0; i < feed_parser.items.size(); i++) {
        const auto& item = feed_parser.items[i];
        
        if (item.description.find(".mp4") != std::string::npos) {
            std::cout << "\n   Found item with .mp4 in description (item " << (i+1) << "):" << std::endl;
            std::cout << "     Title: " << item.title << std::endl;
            std::cout << "     Our extraction found: " << item.extracted_media_urls.size() << " URLs" << std::endl;
            
            // Show the relevant part of description
            size_t mp4_pos = item.description.find(".mp4");
            if (mp4_pos != std::string::npos) {
                size_t start = (mp4_pos > 100) ? mp4_pos - 100 : 0;
                size_t end = std::min(mp4_pos + 200, item.description.size());
                std::cout << "     Description around .mp4:" << std::endl;
                std::cout << "     " << item.description.substr(start, end - start) << std::endl;
            }
            
            break; // Just show the first one
        }
    }
    
    std::cout << "\n=== Debug Complete ===" << std::endl;
    return 0;
}
