#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <fstream>
#include <filesystem>
#include <string>

#include "src/editor/text_editor.hpp"
#include "src/editor/editor.hpp"

class TextEditorTest : public ::testing::Test {
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
};

TEST_F(TextEditorTest, NewEditorDocumentIsNotBlankOrEmpty) {
    rouen::editor::Editor editor;
    
    // Request a new document (e.g. starting editor or File -> New)
    editor.requestNew();
    
    EXPECT_FALSE(editor.empty()) << "Editor should not be empty when a new document is opened!";
    
    // Render a frame with ImGui
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(800, 600));
    editor.render();
    
    // Inspect all active windows in ImGui context
    bool found_editor_child = false;
    for (ImGuiWindow* window : imgui_ctx->Windows) {
        if (window && std::string(window->Name).find("##editor") != std::string::npos) {
            found_editor_child = true;
            break;
        }
    }
    EXPECT_TRUE(found_editor_child) << "Text editor widget child window containing '##editor' should exist and be rendered!";
    
    ImGui::Render();
}

TEST_F(TextEditorTest, SelectFileRendersContentAndIsNotEmpty) {
    // Create a temporary file
    std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "rouen_test_editor_file.txt";
    {
        std::ofstream out(temp_path);
        out << "Hello Rouen Text Editor!\nLine 2 content.";
    }

    rouen::editor::Editor editor;
    editor.select(temp_path.string());

    EXPECT_FALSE(editor.empty());

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(800, 600));
    editor.render();

    bool found_editor_child = false;
    for (ImGuiWindow* window : imgui_ctx->Windows) {
        if (window && std::string(window->Name).find("##editor") != std::string::npos) {
            found_editor_child = true;
            break;
        }
    }
    EXPECT_TRUE(found_editor_child);

    ImGui::Render();

    std::filesystem::remove(temp_path);
}

TEST_F(TextEditorTest, RequestCloseClearsEditor) {
    rouen::editor::Editor editor;
    editor.requestNew();
    EXPECT_FALSE(editor.empty());

    editor.requestClose();
    EXPECT_TRUE(editor.empty()) << "Requesting close on unmodified editor should clear and set empty() to true";
}
