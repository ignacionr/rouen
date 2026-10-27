#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <memory>
#include <string>
#include <vector>

#include "../src/cards/information/rss.hpp"
#include "../src/helpers/glass_top_bar.hpp"
#include "../src/hosts/rss_host.hpp"
#include "../src/models/rss/feed.hpp"
#include "../src/registrar.hpp"

class RSSGalleryLayoutTest : public ::testing::Test {
protected:
    ImGuiContext* imgui_ctx{nullptr};

    void SetUp() override {
        imgui_ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280.0f, 900.0f);
        io.DeltaTime = 1.0f / 60.0f;

        // Build default font atlas so CalcTextSize works
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        // Ensure quitting, notify, and create_card services exist in registrar
        auto quitting_fn = std::make_shared<std::function<bool()>>([]() -> bool { return false; });
        registrar::add("quitting", quitting_fn);

        auto notify_fn = std::make_shared<std::function<void(std::string const&)>>([](std::string const&) {});
        registrar::add("notify", notify_fn);

        auto create_card_fn = std::make_shared<std::function<void(std::string const&)>>([](std::string const&) {});
        registrar::add("create_card", create_card_fn);

        rouen::hosts::RSSHost::no_initial_cards = true;

        // Ensure rss_host service exists in registrar
        try {
            registrar::get<rouen::hosts::RSSHost>("rss_host");
        } catch (...) {
            auto host = std::make_shared<rouen::hosts::RSSHost>();
            registrar::add("rss_host", host);
        }
    }

    void TearDown() override {
        if (imgui_ctx) {
            ImGui::DestroyContext(imgui_ctx);
            imgui_ctx = nullptr;
        }
    }
};

TEST_F(RSSGalleryLayoutTest, TruncateTextLimitsWidthAndAppendsEllipsis) {
    ImGui::NewFrame();

    std::string const short_text = "Tech News";
    std::string const res_short = rouen::cards::rss::truncate_text(short_text, 168.0f);
    EXPECT_EQ(res_short, "Tech News");

    std::string const long_text = "The Quick Brown Fox Jumps Over The Lazy Dog And Runs Around The Entire Forest Repeatedly Without Stopping";
    float const max_w = 168.0f;
    std::string const res_long = rouen::cards::rss::truncate_text(long_text, max_w);

    EXPECT_NE(res_long, long_text);
    EXPECT_TRUE(res_long.ends_with("..."));
    EXPECT_LE(ImGui::CalcTextSize(res_long.c_str()).x, max_w);

    ImGui::EndFrame();
}

TEST_F(RSSGalleryLayoutTest, GlassTopBarContentChildWindowDoesNotEnableHorizontalScroll) {
    ImGuiWindow* child_win = nullptr;

    for (int frame = 0; frame < 2; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(100.0f, 50.0f));
        ImGui::SetNextWindowSize(ImVec2(500.0f, 400.0f));
        ImGui::Begin("TestWindow", nullptr, ImGuiWindowFlags_NoDecoration);

        rouen::helpers::glass_top_bar::render(
            "FeedsScrollAreaTest",
            ImVec2(500.0f, 350.0f),
            []() {
                ImGui::Text("Top Controls");
            },
            [&]() {
                child_win = ImGui::GetCurrentWindow();
                // Add tall content (1000px) but narrow enough to fit horizontally
                ImGui::Dummy(ImVec2(200.0f, 1000.0f));
            }
        );

        ImGui::End();
        ImGui::EndFrame();
    }

    ASSERT_NE(child_win, nullptr);
    // When vertical scrollbar is present, ScrollMax.x should be 0.0f (no horizontal scrolling allowed)
    EXPECT_FLOAT_EQ(child_win->ScrollMax.x, 0.0f);
    EXPECT_FLOAT_EQ(child_win->Scroll.x, 0.0f);
}

TEST_F(RSSGalleryLayoutTest, FeedListAtNonZeroWindowPosDoesNotEnableHorizontalScrollWithLongTitles) {
    rouen::cards::rss rss_card;

    std::vector<std::shared_ptr<media::rss::feed>> feeds;
    for (int i = 0; i < 4; ++i) {
        auto f = std::make_shared<media::rss::feed>();
        f->repo_id = i + 1;
        f->feed_title = "Extremely Long Feed Title Number " + std::to_string(i) +
                        " That Substantially Exceeds One Hundred And Eighty Pixels And Would Cause Runaway Coordinate Overflow In PushTextWrapPos";
        f->source_link = "https://example.com/feed_" + std::to_string(i) + ".xml";
        feeds.push_back(f);
    }

    std::string search_text = "";
    bool has_matches = false;
    ImGuiWindow* child_win = nullptr;

    for (int frame = 0; frame < 2; ++frame) {
        // Render frame with window positioned at X = 450 (e.g. deck column offset)
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(450.0f, 50.0f));
        ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f));
        ImGui::Begin("RSSDeckCard", nullptr, ImGuiWindowFlags_NoDecoration);

        rouen::helpers::glass_top_bar::render(
            "FeedsScrollArea",
            ImVec2(800.0f, 550.0f),
            []() {
                ImGui::Text("Controls");
            },
            [&]() {
                child_win = ImGui::GetCurrentWindow();
                rss_card.render_feed_list(feeds, search_text, has_matches);
            }
        );

        ImGui::End();
        ImGui::EndFrame();
    }

    ASSERT_NE(child_win, nullptr);
    // ScrollMax.x must be 0.0f — no horizontal scrolling allowed
    EXPECT_FLOAT_EQ(child_win->ScrollMax.x, 0.0f);
    EXPECT_FLOAT_EQ(child_win->Scroll.x, 0.0f);
}
