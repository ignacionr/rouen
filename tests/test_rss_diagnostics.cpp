/**
 * Google Test for RSS Diagnostics and Render Performance Metrics
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <memory>

#include "../src/helpers/card_render_metrics.hpp"
#include "../src/hosts/rss_host.hpp"
#include "../src/cards/information/rss.hpp"

namespace rouen::cards {
    std::shared_ptr<hosts::RSSHost> rss::getHost() {
        return nullptr;
    }
}

class RSSDiagnosticsTest : public ::testing::Test {
protected:
    void SetUp() override {
        rouen::helpers::CardRenderMetrics::instance().reset();
    }

    void TearDown() override {
        rouen::helpers::CardRenderMetrics::instance().reset();
    }
};

TEST_F(RSSDiagnosticsTest, CardRenderMetricsRecording) {
    auto& metrics = rouen::helpers::CardRenderMetrics::instance();
    metrics.record("Adventures in DevOps", "rss-feed:68", 1050.0);
    metrics.record("Adventures in DevOps", "rss-feed:68", 950.0);

    auto metric = metrics.get_metric_for_key("rss-feed:68");
    ASSERT_TRUE(metric.has_value());
    EXPECT_EQ(metric->title, "Adventures in DevOps");
    EXPECT_EQ(metric->uri, "rss-feed:68");
    EXPECT_DOUBLE_EQ(metric->last_render_ms, 950.0);
    EXPECT_DOUBLE_EQ(metric->max_render_ms, 1050.0);
    EXPECT_DOUBLE_EQ(metric->min_render_ms, 950.0);
    EXPECT_EQ(metric->render_count, 2);
    EXPECT_EQ(metric->slow_render_count, 2);
    EXPECT_EQ(metric->very_slow_render_count, 2);
}

TEST_F(RSSDiagnosticsTest, CardRenderMetricsInactiveFilter) {
    auto& metrics = rouen::helpers::CardRenderMetrics::instance();
    metrics.record("Test Feed", "rss-feed:99", 25.0);

    auto active = metrics.get_all_metrics(false);
    EXPECT_EQ(active.size(), 1);

    auto all = metrics.get_all_metrics(true);
    EXPECT_EQ(all.size(), 1);
}
