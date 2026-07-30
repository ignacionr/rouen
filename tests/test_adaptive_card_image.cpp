#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "../src/helpers/adaptive_cards/parser.hpp"
#include "../src/helpers/adaptive_cards/renderer.hpp"
#include "../src/helpers/image_cache.hpp"

using namespace rouen::helpers::adaptive_cards;

TEST(AdaptiveCardImageSnapshotTest, OpensCardWithImageAndCreatesSnapshot) {
    // 1. Sample Adaptive Card JSON containing an image element
    const std::string card_json = R"JSON(
{
  "type": "AdaptiveCard",
  "version": "1.3",
  "body": [
    {
      "type": "TextBlock",
      "text": "Adaptive Card Image Test",
      "size": "Large",
      "weight": "Bolder",
      "horizontalAlignment": "Center"
    },
    {
      "type": "Image",
      "url": "https://adaptivecards.io/content/cats/1.png",
      "altText": "Cat Snapshot",
      "size": "Medium",
      "horizontalAlignment": "Center"
    }
  ]
}
)JSON";

    // 2. Parse the card
    parser card_parser{};
    const auto parsed = card_parser.parse(card_json);

    ASSERT_EQ(parsed.type, "AdaptiveCard");
    ASSERT_GE(parsed.body.size(), 2U);
    EXPECT_EQ(parsed.body[1].type, "Image");
    EXPECT_EQ(parsed.body[1].url, "https://adaptivecards.io/content/cats/1.png");

    const std::string img_url = parsed.body[1].url;

    // 3. Initialize ImageCache
    const std::string db_path = "/tmp/test_adaptive_cards_image.db";
    const std::string cache_dir = "/tmp/test_adaptive_cards_images";
    if (std::filesystem::exists(db_path)) std::filesystem::remove(db_path);
    if (std::filesystem::exists(cache_dir)) std::filesystem::remove_all(cache_dir);

    ::helpers::ImageCache image_cache(db_path, cache_dir, 30);

    // 4. Test image download & caching
    std::cout << "[INFO] Testing downloadAndCache for image URL: " << img_url << std::endl;
    bool download_success = image_cache.downloadAndCache(img_url);

    int img_w = 0, img_h = 0;
    bool is_cached = image_cache.isCached(img_url, img_w, img_h);

    // 5. Generate Snapshot Report File
    const std::string snapshot_report_path = "/tmp/adaptive_card_snapshot.json";

    std::ofstream report(snapshot_report_path);
    report << "{\n";
    report << "  \"card_type\": \"" << parsed.type << "\",\n";
    report << "  \"image_url\": \"" << img_url << "\",\n";
    report << "  \"download_success\": " << (download_success ? "true" : "false") << ",\n";
    report << "  \"is_cached\": " << (is_cached ? "true" : "false") << ",\n";
    report << "  \"width\": " << img_w << ",\n";
    report << "  \"height\": " << img_h << "\n";
    report << "}\n";
    report.close();

    std::cout << "[SNAPSHOT REPORT] Written to " << snapshot_report_path << std::endl;

    // Verify snapshot report file exists
    EXPECT_TRUE(std::filesystem::exists(snapshot_report_path));

    std::cout << "[TEST RESULT] Download success: " << download_success
              << " | Is cached: " << is_cached
              << " | Dimensions: " << img_w << "x" << img_h << std::endl;
}

TEST(ImageCacheTest, GrayscaleIsCachedFallback) {
    const std::string db_path = "/tmp/test_image_cache_grayscale.db";
    const std::string cache_dir = "/tmp/test_image_cache_grayscale";
    if (std::filesystem::exists(db_path)) std::filesystem::remove(db_path);
    if (std::filesystem::exists(cache_dir)) std::filesystem::remove_all(cache_dir);

    ::helpers::ImageCache image_cache(db_path, cache_dir, 30);
    const std::string img_url = "https://adaptivecards.io/content/cats/1.png";

    bool download_success = image_cache.downloadAndCache(img_url);
    if (download_success) {
        int w = 0, h = 0;
        EXPECT_TRUE(image_cache.isCached(img_url, w, h, ::helpers::ImageCache::Variant::Color));
        int gw = 0, gh = 0;
        EXPECT_TRUE(image_cache.isCached(img_url, gw, gh, ::helpers::ImageCache::Variant::Grayscale));
        EXPECT_GT(gw, 0);
        EXPECT_GT(gh, 0);
        EXPECT_EQ(w, gw);
        EXPECT_EQ(h, gh);
    }
}
