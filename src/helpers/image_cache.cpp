#include "image_cache.hpp"

#include <cstdio>
#include <format>
#include <fstream>

#include <SDL3_image/SDL_image.h>

#include "fetch.hpp"

namespace helpers {

ImageCache::ImageCache(const std::string& db_path, const std::string& cache_dir, int expiry_days) 
    : db_{db_path}, cache_dir_{cache_dir}, expiry_days_{expiry_days} {
    
    // Create images directory if it doesn't exist
    std::filesystem::create_directories(cache_dir_);
    
    // Ensure the image store table exists
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

RouenGPUTexture* ImageCache::getTexture(SDL_GPUDevice* device, const std::string& url,
                      int& width, int& height, bool force_download,
                      Variant variant) {

    const std::string cache_key = makeCacheKey(url, variant);

    if (!force_download) {
        auto cached_path = getImageFromCache(cache_key, width, height);
        if (cached_path) {
            updateLastAccessed(cache_key);
            return TextureHelper::loadTextureFromFile(device, cached_path->c_str(), width, height);
        }
    }

    if (variant == Variant::Grayscale) {
        int base_width = 0;
        int base_height = 0;
        auto color_path = ensureVariantCached(url, Variant::Color, base_width, base_height, force_download);
        if (!color_path) {
            return nullptr;
        }

        auto grayscale_path = ensureGrayscaleVariant(url, *color_path, base_width, base_height, force_download);
        if (!grayscale_path) {
            return nullptr;
        }

        width = base_width;
        height = base_height;
        updateLastAccessed(cache_key);
        return TextureHelper::loadTextureFromFile(device, grayscale_path->c_str(), width, height);
    }

    auto color_path = ensureVariantCached(url, Variant::Color, width, height, force_download);
    if (!color_path) {
        return nullptr;
    }

    updateLastAccessed(cache_key);
    return TextureHelper::loadTextureFromFile(device, color_path->c_str(), width, height);
}

bool ImageCache::isCached(const std::string& url, int& width, int& height, Variant variant) {
    if (variant == Variant::Grayscale) {
        auto cached_grayscale = getImageFromCache(makeCacheKey(url, variant), width, height);
        if (cached_grayscale.has_value()) {
            return true;
        }
        return getImageFromCache(makeCacheKey(url, Variant::Color), width, height).has_value();
    }
    return getImageFromCache(makeCacheKey(url, variant), width, height).has_value();
}

bool ImageCache::downloadAndCache(const std::string& url) {
    int w = 0, h = 0;
    if (getImageFromCache(makeCacheKey(url, Variant::Color), w, h)) {
        return true;
    }

    return ensureVariantCached(url, Variant::Color, w, h, false).has_value();
}

void ImageCache::clearCache() {
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

void ImageCache::removeFromCache(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& cache_key : {makeCacheKey(url, Variant::Color), makeCacheKey(url, Variant::Grayscale)}) {
        std::string file_path;
        std::string sql = "SELECT file_path FROM image_cache WHERE url = ?";
        db_.exec(sql, [&file_path](sqlite3_stmt* stmt) {
            const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (path) {
                file_path = path;
            }
        }, cache_key);

        if (!file_path.empty() && std::filesystem::exists(file_path)) {
            std::filesystem::remove(file_path);
        }

        sql = "DELETE FROM image_cache WHERE url = ?";
        db_.exec(sql, {}, cache_key);
    }
}

std::string ImageCache::makeCacheKey(const std::string& url, Variant variant) {
    return variant == Variant::Grayscale ? url + "#grayscale" : url;
}

std::filesystem::path ImageCache::buildCacheFilePath(const std::string& url, Variant variant) const {
    const auto cache_key = makeCacheKey(url, variant);
    const auto url_hash = std::hash<std::string>{}(cache_key);
    const char* extension = variant == Variant::Grayscale ? ".png" : ".img";
    return std::filesystem::path(cache_dir_) / (std::to_string(url_hash) + extension);
}

std::optional<std::string> ImageCache::ensureVariantCached(const std::string& url, Variant variant,
                                               int& width, int& height, bool force_download) {
    const std::string cache_key = makeCacheKey(url, variant);
    if (!force_download) {
        auto cached_path = getImageFromCache(cache_key, width, height);
        if (cached_path) {
            return cached_path;
        }
    }

    if (variant == Variant::Color) {
        return downloadColorVariant(url, width, height);
    }

    return std::nullopt;
}

std::optional<std::string> ImageCache::ensureGrayscaleVariant(const std::string& url, const std::string& color_path,
                                                  int& width, int& height, bool force_regenerate) {
    const std::string cache_key = makeCacheKey(url, Variant::Grayscale);
    int cached_width = 0;
    int cached_height = 0;
    if (!force_regenerate) {
        auto cached_path = getImageFromCache(cache_key, cached_width, cached_height);
        if (cached_path) {
            width = cached_width;
            height = cached_height;
            return cached_path;
        }
    }

    const auto grayscale_path = buildCacheFilePath(url, Variant::Grayscale);
    if (!createGrayscaleImage(color_path, grayscale_path.string(), width, height)) {
        return std::nullopt;
    }

    storeImageInCache(cache_key, grayscale_path.string(), width, height);
    return grayscale_path.string();
}

std::optional<std::string> ImageCache::downloadColorVariant(const std::string& url, int& width, int& height) {
    try {
        const auto final_path_obj = buildCacheFilePath(url, Variant::Color);
        const std::string final_path = final_path_obj.string();
        const std::string temp_path = final_path + ".tmp";

        FILE* fp = fopen(temp_path.c_str(), "wb");
        if (!fp) {
            return std::nullopt;
        }

        auto write_callback = [](void* ptr, size_t size, size_t nmemb, void* stream) -> size_t {
            return fwrite(ptr, size, nmemb, static_cast<FILE*>(stream));
        };

        http::fetch fetcher{30};
        fetcher(url, {}, write_callback, fp);
        fclose(fp);

        SDL_Surface* surface = IMG_Load(temp_path.c_str());
        if (!surface) {
            std::filesystem::remove(temp_path);
            return std::nullopt;
        }

        width = surface->w;
        height = surface->h;
        SDL_DestroySurface(surface);

        if (std::filesystem::exists(final_path)) {
            std::filesystem::remove(final_path);
        }
        std::filesystem::rename(temp_path, final_path);

        storeImageInCache(makeCacheKey(url, Variant::Color), final_path, width, height);
        return final_path;
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

bool ImageCache::createGrayscaleImage(const std::string& source_path, const std::string& grayscale_path,
                          int& width, int& height) {
    SDL_Surface* source_surface = IMG_Load(source_path.c_str());
    if (!source_surface) {
        return false;
    }

    SDL_Surface* rgba_surface = SDL_ConvertSurface(source_surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(source_surface);
    if (!rgba_surface) {
        return false;
    }

    width = rgba_surface->w;
    height = rgba_surface->h;

    if (!SDL_LockSurface(rgba_surface)) {
        SDL_DestroySurface(rgba_surface);
        return false;
    }

    auto* pixels = static_cast<Uint32*>(rgba_surface->pixels);
    const int pixel_count = rgba_surface->w * rgba_surface->h;
    const SDL_PixelFormatDetails* format_details = SDL_GetPixelFormatDetails(rgba_surface->format);
    for (int i = 0; i < pixel_count; ++i) {
        Uint8 r = 0, g = 0, b = 0, a = 0;
        SDL_GetRGBA(pixels[i], format_details, nullptr, &r, &g, &b, &a);
        const Uint8 gray = static_cast<Uint8>((77 * r + 150 * g + 29 * b) / 256);
        pixels[i] = SDL_MapRGBA(format_details, nullptr, gray, gray, gray, a);
    }

    SDL_UnlockSurface(rgba_surface);

    if (std::filesystem::exists(grayscale_path)) {
        std::filesystem::remove(grayscale_path);
    }

    const bool saved = IMG_SavePNG(rgba_surface, grayscale_path.c_str());
    SDL_DestroySurface(rgba_surface);
    return saved;
}

std::optional<std::string> ImageCache::getImageFromCache(const std::string& url, int& width, int& height) {
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

bool ImageCache::storeImageInCache(const std::string& url, const std::string& file_path, int width, int height) {
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

void ImageCache::updateLastAccessed(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string sql = "UPDATE image_cache SET last_accessed = datetime('now') WHERE url = ?";
    db_.exec(sql, {}, url);
}

void ImageCache::cleanupExpiredImages() {
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

std::string ImageCache::getMimeType(const std::string& file_path) {
    std::string ext = std::filesystem::path(file_path).extension().string();
    
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".bmp") return "image/bmp";
    if (ext == ".webp") return "image/webp";
    if (ext == ".svg") return "image/svg+xml";
    
    return "image/unknown";
}

} // namespace helpers
