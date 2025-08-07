/**
 * Google Test for RSS Media Extraction
 * Purpose: Test RSS feed parsing and HTML media URL extraction
 * Category: Integration Testing
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string>
#include <vector>

// Include the actual source files we want to test
#include "../src/helpers/html_media_extractor.hpp"
#include "../src/models/rss/feed.hpp"
#include "../src/models/rss/feed_item.hpp"

using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;

class RSSMediaExtractionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup common test data
    }

    void TearDown() override {
        // Cleanup if needed
    }

    // Helper function to create mock RSS XML
    std::string createMockRSSItem(const std::string& enclosure_url, 
                                 const std::string& description, 
                                 const std::string& content_encoded = "") {
        std::string item = R"(
            <item>
                <title>Test Item</title>
                <link>https://example.com/test</link>
                <description><![CDATA[)" + description + R"(]]></description>)";
        
        if (!content_encoded.empty()) {
            item += R"(
                <content:encoded><![CDATA[)" + content_encoded + R"(]]></content:encoded>)";
        }
        
        if (!enclosure_url.empty()) {
            item += R"(
                <enclosure url=")" + enclosure_url + R"(" type="image/jpeg" length="123"/>)";
        }
        
        item += R"(
                <pubDate>Wed, 06 Aug 2025 17:59:51 +0000</pubDate>
            </item>)";
        
        return item;
    }
};

// Test HTML media extraction functionality
class HTMLMediaExtractionTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(HTMLMediaExtractionTest, ExtractsIframeVideoURLs) {
    std::string html_content = R"(<iframe width="560" height="315" src="https://mf.b37mrtl.ru/files/2025.08/689384f185f5400f177257aa.mp4" frameborder="0"></iframe>)";
    
    auto extracted_media = media::html::extract_media_urls(html_content);
    
    ASSERT_THAT(extracted_media, SizeIs(1));
    EXPECT_EQ(extracted_media[0].url, "https://mf.b37mrtl.ru/files/2025.08/689384f185f5400f177257aa.mp4");
    EXPECT_EQ(extracted_media[0].type, "video");
    EXPECT_EQ(extracted_media[0].format, "mp4");
}

TEST_F(HTMLMediaExtractionTest, IgnoresImageTags) {
    std::string html_content = R"(<img alt="Preview" src="https://mf.b37mrtl.ru/files/2025.08/thumbnail/68937cae85f5400a7d102123.jpg" />)";
    
    auto extracted_media = media::html::extract_media_urls(html_content);
    
    EXPECT_THAT(extracted_media, IsEmpty());
}

TEST_F(HTMLMediaExtractionTest, ExtractsVideoTagSources) {
    std::string html_content = R"(<video controls><source src="https://example.com/video.mp4" type="video/mp4"></video>)";
    
    auto extracted_media = media::html::extract_media_urls(html_content);
    
    ASSERT_THAT(extracted_media, SizeIs(1));
    EXPECT_EQ(extracted_media[0].url, "https://example.com/video.mp4");
    EXPECT_EQ(extracted_media[0].type, "video");
    EXPECT_EQ(extracted_media[0].format, "mp4");
}

TEST_F(HTMLMediaExtractionTest, ExtractsYouTubeEmbeds) {
    std::string html_content = R"(<iframe src="https://www.youtube.com/embed/dQw4w9WgXcQ" frameborder="0"></iframe>)";
    
    auto extracted_media = media::html::extract_media_urls(html_content);
    
    ASSERT_THAT(extracted_media, SizeIs(1));
    EXPECT_EQ(extracted_media[0].url, "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    EXPECT_EQ(extracted_media[0].type, "video");
    EXPECT_EQ(extracted_media[0].format, "youtube");
}

TEST_F(HTMLMediaExtractionTest, HandlesComplexContent) {
    std::string html_content = R"(
        <p>Some text content</p>
        <img src="https://example.com/thumbnail.jpg" alt="Thumbnail"/>
        <iframe width="560" height="315" src="https://mf.b37mrtl.ru/files/2025.08/689384f185f5400f177257aa.mp4" frameborder="0"></iframe>
        <a href="https://example.com/link">Some link</a>
        <video controls>
            <source src="https://example.com/video.webm" type="video/webm">
        </video>
    )";
    
    auto extracted_media = media::html::extract_media_urls(html_content);
    
    ASSERT_THAT(extracted_media, SizeIs(2));
    
    // Check that we got both videos and no images
    bool has_mp4 = false, has_webm = false;
    for (const auto& media : extracted_media) {
        if (media.url.find("689384f185f5400f177257aa.mp4") != std::string::npos) {
            has_mp4 = true;
            EXPECT_EQ(media.type, "video");
            EXPECT_EQ(media.format, "mp4");
        } else if (media.url.find("video.webm") != std::string::npos) {
            has_webm = true;
            EXPECT_EQ(media.type, "video");
            EXPECT_EQ(media.format, "webm");
        }
    }
    
    EXPECT_TRUE(has_mp4);
    EXPECT_TRUE(has_webm);
}

// Test media URL validation
TEST_F(HTMLMediaExtractionTest, ValidatesMediaURLs) {
    // Test thumbnail URLs (should return false)
    EXPECT_FALSE(media::html::is_media_url("https://mf.b37mrtl.ru/files/2025.08/thumbnail/68937cae85f5400a7d102123.jpg"));
    EXPECT_FALSE(media::html::is_media_url("https://example.com/image.png"));
    EXPECT_FALSE(media::html::is_media_url("https://example.com/document.pdf"));
    
    // Test video URLs (should return true)
    EXPECT_TRUE(media::html::is_media_url("https://mf.b37mrtl.ru/files/2025.08/689384f185f5400f177257aa.mp4"));
    EXPECT_TRUE(media::html::is_media_url("https://example.com/video.webm"));
    EXPECT_TRUE(media::html::is_media_url("https://example.com/audio.mp3"));
    EXPECT_TRUE(media::html::is_media_url("https://www.youtube.com/watch?v=abc123"));
    EXPECT_TRUE(media::html::is_media_url("https://vimeo.com/123456"));
}

// Test RSS parsing with enclosure validation
TEST_F(RSSMediaExtractionTest, FiltersImageEnclosures) {
    std::string rss_content = R"(<?xml version="1.0" encoding="UTF-8"?>
        <rss version="2.0">
            <channel>
                <title>Test Feed</title>
                <item>
                    <title>Test Item with Image Enclosure</title>
                    <description>Test description</description>
                    <enclosure url="https://mf.b37mrtl.ru/files/2025.08/thumbnail/68937cae85f5400a7d102123.jpg" type="image/jpeg" length="123"/>
                </item>
            </channel>
        </rss>)";
    
    // Parse the RSS feed
    media::rss::feed feed_parser;
    feed_parser(rss_content);
    
    // Check that the feed has items but no media enclosures
    auto items = feed_parser.items;
    ASSERT_THAT(items, SizeIs(1));
    
    // The enclosure should be empty because it's an image, not media
    EXPECT_TRUE(items[0].enclosure.empty());
    
    // But the image should be in the image_url field
    EXPECT_THAT(items[0].image_url, HasSubstr("thumbnail"));
}

TEST_F(RSSMediaExtractionTest, AcceptsVideoEnclosures) {
    std::string rss_content = R"(<?xml version="1.0" encoding="UTF-8"?>
        <rss version="2.0">
            <channel>
                <title>Test Feed</title>
                <item>
                    <title>Test Item with Video Enclosure</title>
                    <description>Test description</description>
                    <enclosure url="https://mf.b37mrtl.ru/files/2025.08/689384f185f5400f177257aa.mp4" type="video/mp4" length="5000000"/>
                </item>
            </channel>
        </rss>)";
    
    // Parse the RSS feed
    media::rss::feed feed_parser;
    feed_parser(rss_content);
    
    // Check that the feed has items with valid media enclosures
    auto items = feed_parser.items;
    ASSERT_THAT(items, SizeIs(1));
    
    // The enclosure should contain the video URL
    EXPECT_THAT(items[0].enclosure, HasSubstr("689384f185f5400f177257aa.mp4"));
}

TEST_F(RSSMediaExtractionTest, ExtractsMediaFromContentEncoded) {
    std::string rss_content = R"(<?xml version="1.0" encoding="UTF-8"?>
        <rss version="2.0">
            <channel>
                <title>Test Feed</title>
                <item>
                    <title>Test Item with Content Encoded</title>
                    <description>Basic description</description>
                    <content:encoded><![CDATA[
                        <p>Some content</p>
                        <iframe width="560" height="315" src="https://mf.b37mrtl.ru/files/2025.08/689384f185f5400f177257aa.mp4" frameborder="0"></iframe>
                    ]]></content:encoded>
                </item>
            </channel>
        </rss>)";
    
    // Parse the RSS feed
    media::rss::feed feed_parser;
    feed_parser(rss_content);
    
    // Check that the feed has items with extracted media
    auto items = feed_parser.items;
    ASSERT_THAT(items, SizeIs(1));
    
    // Check that media was extracted from content:encoded
    ASSERT_THAT(items[0].extracted_media_urls, SizeIs(1));
    EXPECT_THAT(items[0].extracted_media_urls[0].url, HasSubstr("689384f185f5400f177257aa.mp4"));
    EXPECT_EQ(items[0].extracted_media_urls[0].type, "video");
    EXPECT_EQ(items[0].extracted_media_urls[0].format, "mp4");
}

// Integration test simulating RT.com scenario
TEST_F(RSSMediaExtractionTest, RTComScenarioTest) {
    // Simulate RT.com RSS structure with thumbnail enclosures and iframe videos in content
    std::string rt_like_rss = R"(<?xml version="1.0" encoding="UTF-8"?>
        <rss version="2.0">
            <channel>
                <title>RT RSS Feed</title>
                <item>
                    <title>Test RT Article with Video</title>
                    <description><![CDATA[<img alt="Preview" src="https://mf.b37mrtl.ru/files/2025.08/thumbnail/68937cae85f5400a7d102123.jpg" /> Article preview text]]></description>
                    <content:encoded><![CDATA[
                        <p>Article content with embedded video</p>
                        <iframe width="560" height="315" src="https://mf.b37mrtl.ru/files/2025.08/689384f185f5400f177257aa.mp4" frameborder="0"></iframe>
                        <p>More content</p>
                    ]]></content:encoded>
                    <enclosure url="https://mf.b37mrtl.ru/files/2025.08/thumbnail/68937cae85f5400a7d102123.jpg" type="image/jpeg" length="123"/>
                </item>
                <item>
                    <title>Test RT Article without Video</title>
                    <description><![CDATA[<img alt="Preview" src="https://mf.b37mrtl.ru/files/2025.08/thumbnail/another_thumb.jpg" /> Article without video]]></description>
                    <content:encoded><![CDATA[
                        <p>Just text content, no embedded media</p>
                    ]]></content:encoded>
                    <enclosure url="https://mf.b37mrtl.ru/files/2025.08/thumbnail/another_thumb.jpg" type="image/jpeg" length="456"/>
                </item>
            </channel>
        </rss>)";
    
    // Parse the RSS feed
    media::rss::feed feed_parser;
    feed_parser(rt_like_rss);
    
    auto items = feed_parser.items;
    ASSERT_THAT(items, SizeIs(2));
    
    // First item should have extracted video but no enclosure
    EXPECT_TRUE(items[0].enclosure.empty()) << "Thumbnail should not be treated as media enclosure";
    EXPECT_THAT(items[0].image_url, HasSubstr("thumbnail")) << "Thumbnail should be in image_url";
    ASSERT_THAT(items[0].extracted_media_urls, SizeIs(1)) << "Should extract iframe video";
    EXPECT_THAT(items[0].extracted_media_urls[0].url, HasSubstr("689384f185f5400f177257aa.mp4"));
    
    // Second item should have no media at all
    EXPECT_TRUE(items[1].enclosure.empty()) << "Thumbnail should not be treated as media enclosure";
    EXPECT_THAT(items[1].image_url, HasSubstr("thumbnail")) << "Thumbnail should be in image_url";
    EXPECT_THAT(items[1].extracted_media_urls, IsEmpty()) << "Should not extract any media from text-only content";
}

// Performance test for large content
TEST_F(HTMLMediaExtractionTest, HandlesLargeContent) {
    // Create a large HTML content with mixed media
    std::string large_content;
    for (int i = 0; i < 100; ++i) {
        large_content += R"(<p>Lorem ipsum dolor sit amet, consectetur adipiscing elit. </p>)";
        if (i % 10 == 0) {
            large_content += R"(<iframe src="https://example.com/video)" + std::to_string(i) + R"(.mp4"></iframe>)";
        }
        if (i % 15 == 0) {
            large_content += R"(<img src="https://example.com/image)" + std::to_string(i) + R"(.jpg"/>)";
        }
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    auto extracted_media = media::html::extract_media_urls(large_content);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Should extract videos but not images, and should be reasonably fast
    EXPECT_GT(extracted_media.size(), 5); // Should find several videos
    EXPECT_LT(duration.count(), 1000); // Should complete in less than 1 second
    
    // Verify all extracted items are actually videos
    for (const auto& media : extracted_media) {
        EXPECT_THAT(media.url, HasSubstr(".mp4"));
        EXPECT_EQ(media.type, "video");
    }
}
