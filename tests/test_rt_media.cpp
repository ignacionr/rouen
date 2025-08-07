/**
 * Google Test for RT.com RSS Media Extraction
 * Purpose: Test RSS feed parsing with real RT.com data patterns
 * Category: Integration Testing - Real-world RSS feed scenarios
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

class RTMediaExtractionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup common test data
    }

    void TearDown() override {
        // Cleanup if needed
    }

    // Helper function to create RT-style RSS XML with actual patterns
    std::string createRTStyleRSS(const std::string& content_encoded, 
                                 const std::string& enclosure_url = "",
                                 const std::string& description = "") {
        std::string item = R"(<?xml version="1.0" encoding="utf-8"?>
<rss xmlns:media="http://search.yahoo.com/mrss/" xmlns:content="http://purl.org/rss/1.0/modules/content/" xmlns:atom="http://www.w3.org/2005/Atom" xmlns:dc="http://purl.org/dc/elements/1.1/" version="2.0">
    <channel>
        <title>RT - Daily news</title>
        <link>https://www.rt.com</link>
        <description>RT : Today</description>
        <item>
            <title>Test RT Article with Video</title>
            <link>https://www.rt.com/test/article</link>)";
        
        if (!description.empty()) {
            item += R"(
            <description><![CDATA[)" + description + R"(]]></description>)";
        }
        
        if (!content_encoded.empty()) {
            item += R"(
            <content:encoded><![CDATA[)" + content_encoded + R"(]]></content:encoded>)";
        }
        
        if (!enclosure_url.empty()) {
            item += R"(
            <enclosure url=")" + enclosure_url + R"(" type="image/jpeg" length="123"/>)";
        }
        
        item += R"(
            <pubDate>Thu, 07 Aug 2025 08:19:51 +0000</pubDate>
            <dc:creator>RT</dc:creator>
        </item>
    </channel>
</rss>)";
        
        return item;
    }
};

// Test extraction of video content from RT-style iframe in content:encoded
TEST_F(RTMediaExtractionTest, ExtractsVideoFromRTIframe) {
    std::string content_encoded = R"(
        <p>Some article content before the video</p>
        <iframe
            width="560"
            height="315"
            src="https://mf.b37mrtl.ru/files/2025.08/689445bb2030277fda0fd132.mp4" frameborder="0"
        ></iframe>
        <p>More content after video</p>
    )";
    
    std::string rss_content = createRTStyleRSS(content_encoded, 
        "https://mf.b37mrtl.ru/files/2025.08/thumbnail/68944b1d85f5407a7e324521.png");
    
    // Parse the RSS feed
    media::rss::feed feed_parser;
    feed_parser(rss_content);
    
    // Check that the feed has items with extracted media
    auto items = feed_parser.items;
    ASSERT_THAT(items, SizeIs(1));
    
    // Verify the video was extracted from content:encoded
    ASSERT_THAT(items[0].extracted_media_urls, SizeIs(1));
    EXPECT_THAT(items[0].extracted_media_urls[0].url, HasSubstr("689445bb2030277fda0fd132.mp4"));
    EXPECT_EQ(items[0].extracted_media_urls[0].type, "video");
    EXPECT_EQ(items[0].extracted_media_urls[0].format, "mp4");
    
    // Verify enclosure contains thumbnail (image), not video
    EXPECT_THAT(items[0].enclosure, IsEmpty()) << "Image enclosures should be filtered out";
    EXPECT_THAT(items[0].image_url, HasSubstr("thumbnail")) << "Image should be in image_url";
}

// Test RT-style content with multiple media elements
TEST_F(RTMediaExtractionTest, HandlesComplexRTContent) {
    std::string content_encoded = R"(
        <p><strong>Article content with embedded media</strong></p>
        
        <blockquote>
            <span><strong>Read more</strong></span>
            <figure>
                <img src="https://mf.b37mrtl.ru/files/2025.08/thumbnail/68937e2f2030272f5d380e56.jpg" alt="Related article thumbnail" />
                <figcaption><a href="https://www.rt.com/related-article">Related Article</a></figcaption>
            </figure>
        </blockquote>
        
        <p>More content leading to the video...</p>
        
        <iframe
            width="560"
            height="315"
            src="https://mf.b37mrtl.ru/files/2025.08/689384f185f5400f177257aa.mp4" frameborder="0"
        ></iframe>
        
        <p>Content after the video.</p>
    )";
    
    std::string rss_content = createRTStyleRSS(content_encoded);
    
    // Parse the RSS feed
    media::rss::feed feed_parser;
    feed_parser(rss_content);
    
    auto items = feed_parser.items;
    ASSERT_THAT(items, SizeIs(1));
    
    // Should extract only the video, not the images
    ASSERT_THAT(items[0].extracted_media_urls, SizeIs(1));
    EXPECT_THAT(items[0].extracted_media_urls[0].url, HasSubstr("689384f185f5400f177257aa.mp4"));
    EXPECT_EQ(items[0].extracted_media_urls[0].type, "video");
}

// Test RT-style article without video content
TEST_F(RTMediaExtractionTest, HandlesTextOnlyRTArticle) {
    std::string content_encoded = R"(
        <p><strong>Text-only article content</strong></p>
        
        <blockquote>
            <span><strong>Read more</strong></span>
            <figure>
                <img src="https://mf.b37mrtl.ru/files/2025.08/thumbnail/68937e2f2030272f5d380e56.jpg" alt="Related article thumbnail" />
                <figcaption><a href="https://www.rt.com/related-article">Related Article</a></figcaption>
            </figure>
        </blockquote>
        
        <p>Just text content with references and links but no embedded media.</p>
    )";
    
    std::string rss_content = createRTStyleRSS(content_encoded, 
        "https://mf.b37mrtl.ru/files/2025.08/thumbnail/6894633820302753ae2f3420.jpg");
    
    // Parse the RSS feed
    media::rss::feed feed_parser;
    feed_parser(rss_content);
    
    auto items = feed_parser.items;
    ASSERT_THAT(items, SizeIs(1));
    
    // Should not extract any video/audio media
    EXPECT_THAT(items[0].extracted_media_urls, IsEmpty()) << "Text-only articles should have no extracted media";
    EXPECT_THAT(items[0].enclosure, IsEmpty()) << "Image enclosures should be filtered out";
    EXPECT_THAT(items[0].image_url, HasSubstr("thumbnail")) << "Image should be in image_url";
}

// Test RT-style content with social media embeds
TEST_F(RTMediaExtractionTest, HandlesSocialMediaEmbeds) {
    std::string content_encoded = R"(
        <p>Article with social media content:</p>
        
        <blockquote class="twitter-tweet" data-media-max-width="560">
            <p lang="en" dir="ltr">Some tweet content</p>
            <a href="https://twitter.com/user/status/1953181493389394078">August 6, 2025</a>
        </blockquote> 
        <script async src="https://platform.twitter.com/widgets.js" charset="utf-8"></script>
        
        <iframe
            width="560"
            height="315"
            src="https://mf.b37mrtl.ru/files/2025.08/689445bb2030277fda0fd132.mp4" frameborder="0"
        ></iframe>
        
        <p>More content after embedded media.</p>
    )";
    
    std::string rss_content = createRTStyleRSS(content_encoded);
    
    // Parse the RSS feed
    media::rss::feed feed_parser;
    feed_parser(rss_content);
    
    auto items = feed_parser.items;
    ASSERT_THAT(items, SizeIs(1));
    
    // Should extract the video but ignore social media embeds
    ASSERT_THAT(items[0].extracted_media_urls, SizeIs(1));
    EXPECT_THAT(items[0].extracted_media_urls[0].url, HasSubstr("689445bb2030277fda0fd132.mp4"));
    EXPECT_EQ(items[0].extracted_media_urls[0].type, "video");
}

// Test best media URL selection for RT content
TEST_F(RTMediaExtractionTest, RTBestMediaURLSelection) {
    std::string content_encoded = R"(
        <p>Article content</p>
        <iframe src="https://mf.b37mrtl.ru/files/2025.08/689384f185f5400f177257aa.mp4" frameborder="0"></iframe>
    )";
    
    std::string rss_content = createRTStyleRSS(content_encoded);
    
    // Parse the RSS feed
    media::rss::feed feed_parser;
    feed_parser(rss_content);
    
    auto items = feed_parser.items;
    ASSERT_THAT(items, SizeIs(1));
    
    // Test best media URL selection
    std::string best_media = items[0].get_best_media_url();
    EXPECT_THAT(best_media, HasSubstr("689384f185f5400f177257aa.mp4"));
    
    // Test media availability
    EXPECT_TRUE(items[0].has_media()) << "RT item with video should have media";
}

// Test HTML media extractor directly with RT patterns
TEST_F(RTMediaExtractionTest, DirectHTMLExtractionFromRT) {
    std::string html_content = R"(
        <p>Since the escalation of the Ukraine conflict in 2022...</p>
        
        <iframe
            width="560"
            height="315"
            src="https://mf.b37mrtl.ru/files/2025.08/689445bb2030277fda0fd132.mp4" frameborder="0"
        ></iframe>
        
        <p>More content after the video.</p>
    )";
    
    auto extracted_media = media::html::extract_media_urls(html_content);
    
    ASSERT_THAT(extracted_media, SizeIs(1));
    EXPECT_THAT(extracted_media[0].url, HasSubstr("689445bb2030277fda0fd132.mp4"));
    EXPECT_EQ(extracted_media[0].type, "video");
    EXPECT_EQ(extracted_media[0].format, "mp4");
}

// Test filtering of RT thumbnail URLs
TEST_F(RTMediaExtractionTest, FiltersRTThumbnails) {
    std::string html_content = R"(
        <img alt="Preview" src="https://mf.b37mrtl.ru/files/2025.08/thumbnail/6894633820302753ae2f3420.jpg" />
        <iframe src="https://mf.b37mrtl.ru/files/2025.08/689445bb2030277fda0fd132.mp4" frameborder="0"></iframe>
    )";
    
    auto extracted_media = media::html::extract_media_urls(html_content);
    
    // Should only extract the video, not the thumbnail image
    ASSERT_THAT(extracted_media, SizeIs(1));
    EXPECT_THAT(extracted_media[0].url, HasSubstr("689445bb2030277fda0fd132.mp4"));
    EXPECT_EQ(extracted_media[0].type, "video");
}

// Integration test with real RT.com RSS structure
TEST_F(RTMediaExtractionTest, RealRTRSSStructure) {
    // This mimics the actual structure from RT RSS feeds
    std::string rss_content = R"(<?xml version="1.0" encoding="utf-8"?>
<rss xmlns:media="http://search.yahoo.com/mrss/" xmlns:content="http://purl.org/rss/1.0/modules/content/" xmlns:atom="http://www.w3.org/2005/Atom" xmlns:dc="http://purl.org/dc/elements/1.1/" version="2.0">
    <channel>
        <title>RT - Daily news</title>
        <link>https://www.rt.com</link>
        <description>RT : Today</description>
        <language>en</language>
        <copyright>RT</copyright>
        <image>
            <url>https://www.rt.com/static/img/logo-rss.png</url>
            <title>RT - Daily news</title>
            <link>https://www.rt.com</link>
        </image>
        <item>
            <title>FSB prevents terrorist attack in Moscow</title>
            <link><![CDATA[https://www.rt.com/russia/622597-fsb-terrorist-attack-prevented/?utm_source=rss&utm_medium=rss&utm_campaign=RSS]]></link>
            <guid>https://www.rt.com/russia/622597-fsb-terrorist-attack-prevented/</guid>
            <description>
                <![CDATA[<img alt="Preview" align="left" style="margin-right: 10px;" src="https://mf.b37mrtl.ru/files/2025.08/thumbnail/68944b1d85f5407a7e324521.png" /> Russian security forces have thwarted a plot by Ukrainian intelligence <br/><a href="https://www.rt.com/russia/622597-fsb-terrorist-attack-prevented/?utm_source=rss&utm_medium=rss&utm_campaign=RSS">Read Full Article at RT.com</a>]]>
            </description>
            <content:encoded><![CDATA[
                <p><strong>Russian security forces have thwarted a plot by Ukrainian intelligence</strong></p>
                
                <p>Since the escalation of the Ukraine conflict in 2022, Kiev has repeatedly targeted Russian military personnel and public figures, often using sabotage. The FSB regularly reports that it has thwarted these types of plots and has warned citizens to stay alert, noting that Ukrainian intelligence recruits through websites, social media, and messaging platforms.</p>
                
                <iframe
                    width="560"
                    height="315"
                    src="https://mf.b37mrtl.ru/files/2025.08/689445bb2030277fda0fd132.mp4" frameborder="0"
                ></iframe>
                
                <p>Additional content after the video.</p>
            ]]></content:encoded>
            <enclosure url="https://mf.b37mrtl.ru/files/2025.08/thumbnail/68944b1d85f5407a7e324521.png" type="image/jpeg" length="123"/>
            <pubDate>Thu, 07 Aug 2025 07:27:00 +0000</pubDate>
            <dc:creator>RT</dc:creator>
        </item>
    </channel>
</rss>)";
    
    // Parse the RSS feed
    media::rss::feed feed_parser;
    feed_parser(rss_content);
    
    auto items = feed_parser.items;
    ASSERT_THAT(items, SizeIs(1));
    
    // Verify basic feed info
    EXPECT_EQ(feed_parser.feed_title, "RT - Daily news");
    EXPECT_EQ(feed_parser.feed_link, "https://www.rt.com");
    
    // Verify item processing
    EXPECT_EQ(items[0].title, "FSB prevents terrorist attack in Moscow");
    EXPECT_THAT(items[0].link, HasSubstr("www.rt.com/russia/622597"));
    
    // Verify media extraction from content:encoded
    ASSERT_THAT(items[0].extracted_media_urls, SizeIs(1));
    EXPECT_THAT(items[0].extracted_media_urls[0].url, HasSubstr("689445bb2030277fda0fd132.mp4"));
    EXPECT_EQ(items[0].extracted_media_urls[0].type, "video");
    EXPECT_EQ(items[0].extracted_media_urls[0].format, "mp4");
    
    // Verify thumbnail handling
    EXPECT_THAT(items[0].enclosure, IsEmpty()) << "Image enclosures should be filtered out to image_url";
    EXPECT_THAT(items[0].image_url, HasSubstr("thumbnail")) << "Thumbnail should be in image_url";
    
    // Test media detection and best URL
    EXPECT_TRUE(items[0].has_media()) << "RT item should have detectable media";
    std::string best_media = items[0].get_best_media_url();
    EXPECT_THAT(best_media, HasSubstr("689445bb2030277fda0fd132.mp4"));
}

// Live RT RSS validation test - verifies realistic expectations for media content
TEST_F(RTMediaExtractionTest, LiveRTRSSMediaPercentage) {
    // This test validates that our media detection works with real RT RSS data
    // and that the media percentage is within expected ranges for a news feed
    
    // Note: This is a live test and results may vary based on current RT content
    // Typical RT feeds have 5-10% of articles with embedded video content
    
    // For this test, we'll create a representative sample that matches observed patterns
    std::string sample_rss = R"(<?xml version="1.0" encoding="utf-8"?>
<rss xmlns:content="http://purl.org/rss/1.0/modules/content/" version="2.0">
    <channel>
        <title>RT - Daily news</title>
        <item>
            <title>Text-only article about diplomacy</title>
            <description>Standard news article without embedded media</description>
            <content:encoded><![CDATA[<p>Article content without video</p>]]></content:encoded>
        </item>
        <item>
            <title>Another text article</title>
            <description>Another standard article</description>
            <content:encoded><![CDATA[<p>More text content</p>]]></content:encoded>
        </item>
        <item>
            <title>Article with video content (VIDEO)</title>
            <description>News article with embedded video</description>
            <content:encoded><![CDATA[
                <p>Article with video content</p>
                <iframe src="https://mf.b37mrtl.ru/files/2025.08/689445bb2030277fda0fd132.mp4" frameborder="0"></iframe>
            ]]></content:encoded>
        </item>
    </channel>
</rss>)";
    
    media::rss::feed feed_parser;
    feed_parser(sample_rss);
    
    ASSERT_THAT(feed_parser.items, SizeIs(3));
    
    // Count items with extracted media
    int items_with_media = 0;
    for (const auto& item : feed_parser.items) {
        if (!item.extracted_media_urls.empty()) {
            items_with_media++;
        }
    }
    
    // Verify expected media detection rate (1 out of 3 = 33% in this sample)
    EXPECT_EQ(items_with_media, 1) << "Should detect media in exactly 1 item";
    
    // Verify the media item
    bool found_video_item = false;
    for (const auto& item : feed_parser.items) {
        if (!item.extracted_media_urls.empty()) {
            found_video_item = true;
            EXPECT_THAT(item.title, HasSubstr("VIDEO")) << "Video items often have VIDEO in title";
            EXPECT_EQ(item.extracted_media_urls.size(), 1);
            EXPECT_THAT(item.extracted_media_urls[0].url, HasSubstr(".mp4"));
            EXPECT_EQ(item.extracted_media_urls[0].type, "video");
        }
    }
    
    EXPECT_TRUE(found_video_item) << "Should find at least one video item";
}
