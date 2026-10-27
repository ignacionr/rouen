#include <gtest/gtest.h>
#include "../src/hosts/auto_update_host.hpp"

TEST(AutoUpdateTest, SemverParsingAndComparison) {
    int major = 0, minor = 0, patch = 0;
    EXPECT_TRUE(rouen::hosts::AutoUpdateHost::parse_semver("v1.4.19", major, minor, patch));
    EXPECT_EQ(major, 1);
    EXPECT_EQ(minor, 4);
    EXPECT_EQ(patch, 19);

    EXPECT_TRUE(rouen::hosts::AutoUpdateHost::parse_semver("1.5.0-beta1", major, minor, patch));
    EXPECT_EQ(major, 1);
    EXPECT_EQ(minor, 5);
    EXPECT_EQ(patch, 0);

    // Comparisons
    EXPECT_TRUE(rouen::hosts::AutoUpdateHost::compare_versions("1.4.19", "v1.4.20"));
    EXPECT_TRUE(rouen::hosts::AutoUpdateHost::compare_versions("1.4.19", "1.5.0"));
    EXPECT_FALSE(rouen::hosts::AutoUpdateHost::compare_versions("1.4.20", "v1.4.19"));
    EXPECT_FALSE(rouen::hosts::AutoUpdateHost::compare_versions("1.4.19", "1.4.19"));
}

TEST(AutoUpdateTest, PlatformAssetSelection) {
    std::string sample_json = R"({
        "tag_name": "v1.4.20",
        "assets": [
            {"browser_download_url": "https://github.com/ignacionr/rouen/releases/download/v1.4.20/rouen-windows-x64.zip"},
            {"browser_download_url": "https://github.com/ignacionr/rouen/releases/download/v1.4.20/rouen-macos-universal.dmg"},
            {"browser_download_url": "https://github.com/ignacionr/rouen/releases/download/v1.4.20/rouen-linux-x64.tar.gz"}
        ]
    })";

    std::string selected = rouen::hosts::AutoUpdateHost::select_platform_asset(sample_json);
    EXPECT_FALSE(selected.empty());
#if defined(_WIN32)
    EXPECT_NE(selected.find("rouen-windows-x64.zip"), std::string::npos);
#elif defined(__APPLE__)
    EXPECT_NE(selected.find("rouen-macos-universal.dmg"), std::string::npos);
#else
    EXPECT_NE(selected.find("rouen-linux-x64.tar.gz"), std::string::npos);
#endif
}

TEST(AutoUpdateTest, ScheduleCalculationWindow) {
    auto now = std::chrono::system_clock::now();
    int offset_mins = -1;
    auto next_time = rouen::hosts::AutoUpdateHost::calculate_next_check_time(now, 0, 180, &offset_mins);

    EXPECT_GE(offset_mins, 0);
    EXPECT_LE(offset_mins, 180);
    EXPECT_GE(next_time, now);
}

TEST(AutoUpdateTest, HostStatusAndToggle) {
    rouen::hosts::AutoUpdateHost host;
    EXPECT_TRUE(host.is_enabled());

    host.set_enabled(false);
    EXPECT_FALSE(host.is_enabled());

    auto status = host.get_status();
    EXPECT_FALSE(status.enabled);
    EXPECT_EQ(status.current_version, "1.4.19");
}
