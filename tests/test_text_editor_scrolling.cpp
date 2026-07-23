#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "../external/imguicolortextedit/TextEditor.h"
#include <string>
#include <vector>

class TextEditorScrollingTest : public ::testing::Test {
protected:
    ImGuiContext* imgui_ctx = nullptr;

    void SetUp() override {
        imgui_ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1024.0f, 768.0f);
        
        // Build font atlas
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }

    void TearDown() override {
        if (imgui_ctx) {
            ImGui::DestroyContext(imgui_ctx);
            imgui_ctx = nullptr;
        }
    }

    std::string generate_lines(int count) {
        std::string result;
        for (int i = 1; i <= count; ++i) {
            result += "Line " + std::to_string(i) + " - Sample text content line " + std::to_string(i) + "\n";
        }
        return result;
    }
};

// Test 1: Moving caret to lines inside visible viewport MUST NOT scroll the viewport
TEST_F(TextEditorScrollingTest, CaretMovementInsideVisibleBoundsMustNotScroll) {
    TextEditor editor;
    editor.SetText(generate_lines(100));
    editor.SetCursorPosition(TextEditor::Coordinates(0, 0));

    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;

    // Initial render pass
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(500, 300));
    ImGui::Begin("TestWindow", nullptr, ImGuiWindowFlags_NoDecoration);
    editor.Render("##test_editor", ImVec2(480, 280), true);
    ImGui::End();
    ImGui::Render();

    EXPECT_FLOAT_EQ(editor.GetLastScrollY(), 0.0f) << "Initial scroll Y should be 0";

    // Step through lines 1 to 18 (which fit in the 250px usable height viewport)
    for (int line = 1; line <= 18; ++line) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(500, 300));
        ImGui::Begin("TestWindow", nullptr, ImGuiWindowFlags_NoDecoration);
        editor.MoveDown(1, false);
        editor.Render("##test_editor", ImVec2(480, 280), true);
        ImGui::End();
        ImGui::Render();
    }

    // Inspect scroll Y on next frame pass
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(500, 300));
    ImGui::Begin("TestWindow", nullptr, ImGuiWindowFlags_NoDecoration);
    editor.Render("##test_editor", ImVec2(480, 280), true);
    float scroll_y_at_line_18 = editor.GetLastScrollY();
    ImGui::End();
    ImGui::Render();

    EXPECT_FLOAT_EQ(scroll_y_at_line_18, 0.0f) 
        << "Viewport scrolled prematurely at line 18 while line 18 was still inside the visible viewport!";
}

// Test 2: Moving caret past the bottom visible boundary MUST scroll the viewport
TEST_F(TextEditorScrollingTest, CaretMovementPastBottomBoundaryMustScroll) {
    TextEditor editor;
    editor.SetText(generate_lines(100));
    editor.SetCursorPosition(TextEditor::Coordinates(0, 0));

    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;

    // Initial render pass
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(500, 300));
    ImGui::Begin("TestWindow", nullptr, ImGuiWindowFlags_NoDecoration);
    editor.Render("##test_editor", ImVec2(480, 280), true);
    ImGui::End();
    ImGui::Render();

    // Move down 50 lines to force caret past bottom viewport boundary
    for (int i = 0; i < 50; ++i) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(500, 300));
        ImGui::Begin("TestWindow", nullptr, ImGuiWindowFlags_NoDecoration);
        editor.MoveDown(1, false);
        editor.Render("##test_editor", ImVec2(480, 280), true);
        ImGui::End();
        ImGui::Render();
    }

    // Inspect scroll Y on next frame pass
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(500, 300));
    ImGui::Begin("TestWindow", nullptr, ImGuiWindowFlags_NoDecoration);
    editor.Render("##test_editor", ImVec2(480, 280), true);
    float scroll_y_at_line_50 = editor.GetLastScrollY();
    ImGui::End();
    ImGui::Render();

    EXPECT_GT(scroll_y_at_line_50, 0.0f) 
        << "Viewport should scroll down when caret moves past the bottom boundary to line 50!";
}
