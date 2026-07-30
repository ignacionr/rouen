#pragma once

#include <string>
#include <filesystem>
#include <optional>
#include <mutex>
#include <vector>

#include <SDL3/SDL.h>

#include "sqlite.hpp"
#include "texture_helper.hpp"

namespace helpers {

/**
 * ImageCache class - Downloads, caches and retrieves images from URLs
 */
class ImageCache {
public:
    enum class Variant {
        Color,
        Grayscale,
    };

    /**
     * Constructor - initializes the database connection and ensures tables exist
     */
    ImageCache(const std::string& db_path, const std::string& cache_dir, int expiry_days = 30);
    ~ImageCache() = default;

    /**
     * Get or download an image and convert it to an SDL_GPUTexture
     */
    RouenGPUTexture* getTexture(SDL_GPUDevice* device, const std::string& url,
                          int& width, int& height, bool force_download = false,
                          Variant variant = Variant::Color);

    /**
     * Check if an image is already stored in the cache
     */
    bool isCached(const std::string& url, int& width, int& height, Variant variant = Variant::Color);

    /**
     * Downloads an image in the background, verifies if it's a valid image (CPU only),
     * and stores it in the cache database.
     */
    bool downloadAndCache(const std::string& url);

    /**
     * Clears the entire image cache
     */
    void clearCache();

    /**
     * Removes a specific image from the cache
     */
    void removeFromCache(const std::string& url);

private:
    static std::string makeCacheKey(const std::string& url, Variant variant);

    std::filesystem::path buildCacheFilePath(const std::string& url, Variant variant) const;

    std::optional<std::string> ensureVariantCached(const std::string& url, Variant variant,
                                                   int& width, int& height, bool force_download);

    std::optional<std::string> ensureGrayscaleVariant(const std::string& url, const std::string& color_path,
                                                      int& width, int& height, bool force_regenerate);

    std::optional<std::string> downloadColorVariant(const std::string& url, int& width, int& height);

    bool createGrayscaleImage(const std::string& source_path, const std::string& grayscale_path,
                              int& width, int& height);

    std::optional<std::string> getImageFromCache(const std::string& url, int& width, int& height);

    bool storeImageInCache(const std::string& url, const std::string& file_path, int width, int height);

    void updateLastAccessed(const std::string& url);

    void cleanupExpiredImages();

    std::string getMimeType(const std::string& file_path);

private:
    hosting::db::sqlite db_;
    std::string cache_dir_;
    int expiry_days_;
    std::mutex mutex_;
};

} // namespace helpers
