#include <gtest/gtest.h>
#include <imgui.h>
#include "../src/cards/interface/card.hpp"
#include "../src/cards/interface/deck.hpp"
#include "../src/helpers/card_render_metrics.hpp"
#include <memory>
#include <vector>

namespace {

class DummyCard : public card {
public:
    explicit DummyCard(std::string name_str, float card_width = 300.0f) {
        width = card_width;
        name(name_str);
    }

    bool render(rouen::ui::ui_context& /*ui*/) override {
        return true;
    }

    std::string get_uri() const override {
        return window_title;
    }
};

class DeckScrollingTest : public ::testing::Test {
protected:
    ImGuiContext* imgui_ctx{nullptr};

    void SetUp() override {
        imgui_ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1200.0f, 800.0f); // 1200px viewport
    }

    void TearDown() override {
        if (imgui_ctx) {
            ImGui::DestroyContext(imgui_ctx);
            imgui_ctx = nullptr;
        }
    }
};

TEST_F(DeckScrollingTest, VerifyDirectPage3ToPage1ScrollTargeting) {
    deck test_deck(nullptr);

    // Create 3 pages of cards:
    // Page 1 (Section 0, abs_x 0 to 1200): Card 1, Card 2, Card 3, Card 4
    // Page 2 (Section 1, abs_x 1200 to 2400): Card 5, Card 6, Card 7
    // Page 3 (Section 2, abs_x 2400 to 3600): Card 8 (only card on Page 3)

    auto c1 = std::make_shared<DummyCard>("Card 1", 300.0f);
    auto c2 = std::make_shared<DummyCard>("Card 2", 300.0f);
    auto c3 = std::make_shared<DummyCard>("Card 3", 300.0f);
    auto c4 = std::make_shared<DummyCard>("Card 4", 300.0f); // Ends at 1200 (Page 1)

    auto c5 = std::make_shared<DummyCard>("Card 5", 300.0f); // Starts at 1200 (Page 2)
    auto c6 = std::make_shared<DummyCard>("Card 6", 300.0f);
    auto c7 = std::make_shared<DummyCard>("Card 7", 300.0f); // Ends at 2100 -> expanded to 2400 (Page 2)

    auto c8 = std::make_shared<DummyCard>("Card 8", 300.0f); // Starts at 2400 (Page 3)

    test_deck.get_cards().push_back(c1);
    test_deck.get_cards().push_back(c2);
    test_deck.get_cards().push_back(c3);
    test_deck.get_cards().push_back(c4);

    test_deck.get_cards().push_back(c5);
    test_deck.get_cards().push_back(c6);
    test_deck.get_cards().push_back(c7);

    test_deck.get_cards().push_back(c8);

    // 1. Initial State: Card 8 (Page 3) has grab_focus
    c8->grab_focus = true;

    ImGui::NewFrame();
    (void)test_deck.render();
    ImGui::Render();

    // Target scroll must be Page 3 (2400.0f)
    EXPECT_NEAR(test_deck.get_target_scroll_x(), 2400.0f, 1.0f);

    // 2. Now user selects Card 4 (on Page 1) while on Page 3
    c8->grab_focus = false;
    c8->is_focused = false;
    c4->grab_focus = true;

    ImGui::NewFrame();
    (void)test_deck.render();
    ImGui::Render();

    // Target scroll must IMMEDIATELY switch to Page 1 (0.0f) despite Card 5/6/7 being in between
    EXPECT_NEAR(test_deck.get_target_scroll_x(), 0.0f, 1.0f);
    EXPECT_TRUE(c4->is_visible_in_viewport); // Card 4 must NOT be culled while grab_focus is true

    // 3. Simulate intermediate frames of smooth scrolling towards Page 1
    for (int frame = 0; frame < 15; ++frame) {
        ImGui::NewFrame();
        (void)test_deck.render();
        ImGui::Render();

        // target_scroll_x must stay locked at 0.0f (Page 1) until current_scroll_x reaches Page 1
        EXPECT_NEAR(test_deck.get_target_scroll_x(), 0.0f, 1.0f);
    }

    // 4. Verify user can now jump from Page 1 directly back to Card 8 on Page 3
    c4->grab_focus = false;
    c4->is_focused = false;
    c8->grab_focus = true;

    ImGui::NewFrame();
    (void)test_deck.render();
    ImGui::Render();

    EXPECT_NEAR(test_deck.get_target_scroll_x(), 2400.0f, 1.0f);
    EXPECT_TRUE(c8->is_visible_in_viewport); // Card 8 must NOT be culled while grab_focus is true
}

} // namespace
