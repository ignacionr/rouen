/**
 * Simple test to debug why some RT items don't extract media
 */

#include <iostream>
#include <string>
#include <curl/curl.h>
#include "src/helpers/html_media_extractor.hpp"
#include "src/models/rss/feed.hpp"

// Callback for CURL to write data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data) {
    size_t total_size = size * nmemb;
    data->append(static_cast<char*>(contents), total_size);
    return total_size;
}

int main() {
    // Fetch RSS content
    CURL* curl = curl_easy_init();
    std::string response;
    
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://www.rt.com/rss/");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    
    // Parse the feed
    media::rss::feed feed_parser;
    feed_parser(response);
    
    std::cout << "=== Simple RT Media Detection Test ===" << std::endl;
    std::cout << "Total items: " << feed_parser.items.size() << std::endl;
    
    int items_with_media = 0;
    for (size_t i = 0; i < feed_parser.items.size(); i++) {
        const auto& item = feed_parser.items[i];
        
        if (!item.extracted_media_urls.empty()) {
            items_with_media++;
            std::cout << "\nItem " << (i+1) << " HAS MEDIA:" << std::endl;
            std::cout << "  Title: " << item.title.substr(0, 60) << "..." << std::endl;
            std::cout << "  Media URLs found: " << item.extracted_media_urls.size() << std::endl;
            
            for (const auto& media : item.extracted_media_urls) {
                std::cout << "    - " << media.type << " (" << media.format << "): " << media.url << std::endl;
            }
        }
    }
    
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Items with media: " << items_with_media << " out of " << feed_parser.items.size() << std::endl;
    std::cout << "Percentage with media: " << (100.0 * items_with_media / feed_parser.items.size()) << "%" << std::endl;
    
    return 0;
}
