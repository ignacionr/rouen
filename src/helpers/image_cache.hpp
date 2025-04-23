#pragma once

#include <string>
#include <filesystem>
#include <optional>
#include <mutex>
#include <vector>
#include <fstream>
#include <SDL2/SDL.h>

#include "fetch.hpp"
#include "sqlite.hpp"
#include "texture_helper.hpp"

namespace helpers {

/**
 * ImageCache class - Downloads, caches and retrieves images from URLs
 * 
 * This class handles:
 * 1. Downloading images from URLs using http::fetch
 * 2. Storing image data in SQLite database
 * 3. Retrieving cached images when available
 * 4. Loading images as SDL textures for rendering
 */
class ImageCache {
public:
    /**
     * Constructor - initializes the database connection and ensures tables exist
     * 
     * @param db_path Path to the SQLite database file
     * @param cache_dir Directory to temporarily store image files
     * @param expiry_days Number of days before cached images expire (0 = never expire)
     */
    ImageCache(const std::string& db_path, const std::string& cache_dir, int expiry_days = 30) 
        : db_{db_path}, cache_dir_{cache_dir}, expiry_days_{expiry_days}, fetcher_{30} {
        
        // Create images directory if it doesn't exist
        std::filesystem::create_directories(cache_dir_);
        
        // Ensure the image store table exists
        // Note: We store the file path rather than the binary data due to SQLite wrapper limitations
        db_.ensure_table("image_cache", 
            "url TEXT PRIMARY KEY, "
            "file_path TEXT, "
            "width INTEGER, "
            "height INTEGER, "
            "mime_type TEXT, "
            "fetched_at TEXT DEFAULT (datetime('now')), "
            "last_accessed TEXT DEFAULT (datetime('now'))"
        );
        
        // Cleanup expired images if expiry_days > 0
        if (expiry_days_ > 0) {
            cleanupExpiredImages();
        }
    }
    
    /**
     * Get or download an image and convert it to an SDL_Texture
     * 
     * @param renderer SDL_Renderer to use for texture creation
     * @param url URL of the image to retrieve
     * @param width Output parameter for image width
     * @param height Output parameter for image height
     * @param force_download Force download even if cached
     * @return SDL_Texture pointer if successful, nullptr if failed
     */
    SDL_Texture* getTexture(SDL_Renderer* renderer, const std::string& url, 
                          int& width, int& height, bool force_download = false) {
        
        // Try to get the image from cache if not forcing download
        if (!force_download) {
            auto cached_path = getImageFromCache(url, width, height);
            if (cached_path) {
                // Update the last accessed time
                updateLastAccessed(url);
                
                // Load texture from cached file
                return TextureHelper::loadTextureFromFile(renderer, cached_path->c_str(), width, height);
            }
        }
        
        // Image not in cache or forcing download, download it
        try {
            // Generate a filename based on URL hash
            auto url_hash = std::hash<std::string>{}(url);
            std::string final_path = std::filesystem::path(cache_dir_) / (std::to_string(url_hash) + ".img");
            std::string temp_path = final_path + ".tmp";
            
            // Create file for writing
            FILE* fp = fopen(temp_path.c_str(), "wb");
            if (!fp) {
                return nullptr;
            }
            
            // Set up callback to write to file
            auto write_callback = [](void* ptr, size_t size, size_t nmemb, void* stream) -> size_t {
                return fwrite(ptr, size, nmemb, (FILE*)stream);
            };
            
            // Download the image
            fetcher_(url, {}, write_callback, fp);
            
            // Close the file
            fclose(fp);
            
            // Load the image to get dimensions
            SDL_Texture* texture = TextureHelper::loadTextureFromFile(renderer, temp_path.c_str(), width, height);
            
            if (texture) {
                // Move the temporary file to its final location
                if (std::filesystem::exists(final_path)) {
                    std::filesystem::remove(final_path);
                }
                std::filesystem::rename(temp_path, final_path);
                
                // Store info in cache
                storeImageInCache(url, final_path, width, height);
                
                return texture;
            }
            
            // Cleanup temp file on failure
            std::filesystem::remove(temp_path);
            return nullptr;
        }
        catch (const std::exception& e) {
            return nullptr;
        }
    }
    
    /**
     * Clears the entire image cache
     */
    void clearCache() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Get all file paths first
        std::vector<std::string> file_paths;
        std::string sql = "SELECT file_path FROM image_cache";
        db_.exec(sql, [&file_paths](sqlite3_stmt* stmt) {
            const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (path) {
                file_paths.push_back(path);
            }
        });
        
        // Delete all files
        for (const auto& path : file_paths) {
            if (std::filesystem::exists(path)) {
                std::filesystem::remove(path);
            }
        }
        
        // Delete all records from the table
        db_.exec("DELETE FROM image_cache");
        
        // Vacuum the database to reclaim space
        db_.exec("VACUUM");
    }
    
    /**
     * Removes a specific image from the cache
     * 
     * @param url URL of the image to remove
     */
    void removeFromCache(const std::string& url) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Get the file path first
        std::string file_path;
        std::string sql = "SELECT file_path FROM image_cache WHERE url = ?";
        db_.exec(sql, [&file_path](sqlite3_stmt* stmt) {
            const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (path) {
                file_path = path;
            }
        }, url);
        
        // Delete the file if it exists
        if (!file_path.empty() && std::filesystem::exists(file_path)) {
            std::filesystem::remove(file_path);
        }
        
        // Delete the record
        sql = "DELETE FROM image_cache WHERE url = ?";
        db_.exec(sql, {}, url);
    }

private:
    /**
     * Attempts to retrieve an image from the cache
     * 
     * @param url URL of the image to retrieve
     * @param width Output parameter to store image width
     * @param height Output parameter to store image height
     * @return Optional path to the image file, or empty if not in cache
     */
    std::optional<std::string> getImageFromCache(const std::string& url, int& width, int& height) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::optional<std::string> result;
        std::string sql = "SELECT file_path, width, height FROM image_cache WHERE url = ?";
        
        db_.exec(sql, [&result, &width, &height](sqlite3_stmt* stmt) {
            const char* file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            
            // Get dimensions
            width = sqlite3_column_int(stmt, 1);
            height = sqlite3_column_int(stmt, 2);
            
            if (file_path && std::filesystem::exists(file_path)) {
                result = file_path;
            }
        }, url);
        
        return result;
    }
    
    /**
     * Stores image information in the cache
     * 
     * @param url URL of the image
     * @param file_path Path to the stored image file
     * @param width Width of the image
     * @param height Height of the image
     * @return True if successful, false if failed
     */
    bool storeImageInCache(const std::string& url, const std::string& file_path, int width, int height) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // Get mime type
            std::string mime_type = getMimeType(file_path);
            
            // Store in database
            std::string sql = "INSERT OR REPLACE INTO image_cache (url, file_path, width, height, mime_type, fetched_at, last_accessed) "
                              "VALUES (?, ?, ?, ?, ?, datetime('now'), datetime('now'))";
            
            db_.exec(sql, {}, url, file_path, width, height, mime_type);
            
            return true;
        }
        catch (const std::exception&) {
            return false;
        }
    }
    
    /**
     * Updates the last accessed timestamp for a cached image
     * 
     * @param url URL of the image to update
     */
    void updateLastAccessed(const std::string& url) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string sql = "UPDATE image_cache SET last_accessed = datetime('now') WHERE url = ?";
        db_.exec(sql, {}, url);
    }
    
    /**
     * Deletes expired images from the cache
     */
    void cleanupExpiredImages() {
        if (expiry_days_ <= 0) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Get expired file paths first
        std::vector<std::string> expired_files;
        std::string sql = std::format(
            "SELECT file_path FROM image_cache WHERE last_accessed < datetime('now', '-{} days')",
            expiry_days_
        );
        
        db_.exec(sql, [&expired_files](sqlite3_stmt* stmt) {
            const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (path) {
                expired_files.push_back(path);
            }
        });
        
        // Delete all expired files
        for (const auto& path : expired_files) {
            if (std::filesystem::exists(path)) {
                std::filesystem::remove(path);
            }
        }
        
        // Delete records from database
        sql = std::format(
            "DELETE FROM image_cache WHERE last_accessed < datetime('now', '-{} days')",
            expiry_days_
        );
        db_.exec(sql);
    }
    
    /**
     * Tries to determine the MIME type of a file based on extension
     * 
     * @param file_path Path to the file
     * @return MIME type string (default: "image/unknown")
     */
    std::string getMimeType(const std::string& file_path) {
        std::string ext = std::filesystem::path(file_path).extension().string();
        
        if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
        if (ext == ".png") return "image/png";
        if (ext == ".gif") return "image/gif";
        if (ext == ".bmp") return "image/bmp";
        if (ext == ".webp") return "image/webp";
        if (ext == ".svg") return "image/svg+xml";
        
        return "image/unknown";
    }

private:
    hosting::db::sqlite db_;
    std::string cache_dir_;
    int expiry_days_;
    http::fetch fetcher_;
    std::mutex mutex_;
};

} // namespace helpers