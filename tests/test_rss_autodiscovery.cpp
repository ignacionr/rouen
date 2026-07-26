/**
 * Google Test for RSS Autodiscovery
 * Purpose: Test HTML RSS link extraction and webpage RSS URL resolution
 * Category: Unit/Integration Testing
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string>

#include "../src/hosts/rss_host.hpp"

TEST(RSSAutodiscoveryTest, ExtractRssFromFrequentMilerHtml) {
    std::string html = R"(
        <!DOCTYPE html>
        <html>
        <head>
            <title>Frequent Miler</title>
            <link rel="alternate" type="application/rss+xml" title="Frequent Miler &raquo; Feed" href="https://frequentmiler.com/feed/" />
            <link rel="alternate" type="application/rss+xml" title="Frequent Miler &raquo; Comments Feed" href="https://frequentmiler.com/comments/feed/" />
        </head>
        <body><h1>Frequent Miler</h1></body>
        </html>
    )";

    std::string rss_url = rouen::hosts::extractRssUrlFromHtml(html, "https://frequentmiler.com");
    EXPECT_EQ(rss_url, "https://frequentmiler.com/feed/");
}

TEST(RSSAutodiscoveryTest, ResolveRelativeUrl) {
    std::string html = R"(
        <html>
        <head>
            <link rel="alternate" type="application/atom+xml" href="/feeds/main" />
        </head>
        </html>
    )";

    std::string rss_url = rouen::hosts::extractRssUrlFromHtml(html, "https://daringfireball.net");
    EXPECT_EQ(rss_url, "https://daringfireball.net/feeds/main");
}

TEST(RSSAutodiscoveryTest, ResolveFeedUrlFrequentMilerLive) {
    std::string resolved = rouen::hosts::resolveFeedUrl("https://frequentmiler.com");
    EXPECT_EQ(resolved, "https://frequentmiler.com/feed/");
}
