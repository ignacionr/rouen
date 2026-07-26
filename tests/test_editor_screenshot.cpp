#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <cmath>

#include "src/editor/text_editor.hpp"
#include "src/editor/editor.hpp"
#include "src/helpers/capture_helper.hpp"
#include "src/registrar.hpp"

class EditorScreenshotTest : public ::testing::Test {
protected:
    ImGuiContext* imgui_ctx = nullptr;
    SDL_GPUDevice* gpu_device = nullptr;

    void SetUp() override {
        SDL_Init(SDL_INIT_VIDEO);
        gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_PRIVATE, true, nullptr);
        if (gpu_device) {
            registrar::add<SDL_GPUDevice*>("main_gpu_device", std::make_shared<SDL_GPUDevice*>(gpu_device));
        }

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



    size_t count_non_background_pixels(SDL_Surface* surface, Uint32 bg_color) {
        if (!surface || !surface->pixels) return 0;
        size_t count = 0;
        Uint32* pixels = static_cast<Uint32*>(surface->pixels);
        size_t total = static_cast<size_t>(surface->w * surface->h);
        for (size_t i = 0; i < total; ++i) {
            if (pixels[i] != bg_color) {
                count++;
            }
        }
        return count;
    }

    size_t count_surface_pixel_differences(SDL_Surface* s1, SDL_Surface* s2) {
        if (!s1 || !s2 || s1->w != s2->w || s1->h != s2->h) return 0;
        size_t diff_count = 0;
        Uint32* p1 = static_cast<Uint32*>(s1->pixels);
        Uint32* p2 = static_cast<Uint32*>(s2->pixels);
        size_t total = static_cast<size_t>(s1->w * s1->h);
        for (size_t i = 0; i < total; ++i) {
            if (p1[i] != p2[i]) {
                diff_count++;
            }
        }
        return diff_count;
    }
};

TEST_F(EditorScreenshotTest, EditExistingTextFileObtainScreenshotAndCompare) {
    // 1. Create an existing text file with known content
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    std::filesystem::path file_path = temp_dir / "rouen_existing_file_test.cpp";
    
    std::string known_initial_text = 
        "#include <iostream>\n"
        "// Known initial test line 1\n"
        "int main() {\n"
        "    std::cout << \"Hello Rouen Screenshot Test!\" << std::endl;\n"
        "    return 0;\n"
        "}\n";

    {
        std::ofstream out(file_path);
        out << known_initial_text;
    }

    // 2. Open the file in the Editor
    rouen::editor::Editor editor;
    editor.select(file_path.string());

    EXPECT_FALSE(editor.empty()) << "Editor should not be empty after selecting an existing file";

    // 3. Edit the existing text file (append additional text)
    std::string edited_text = known_initial_text + "\n// EDITED_LINE_ADDED_DURING_TEST: 123456789\n";
    {
        std::ofstream out(file_path);
        out << edited_text;
    }
    // Re-select to reflect edits in the editor
    editor.select(file_path.string());

    // 4. Test snapshot via registered service in registrar (API capability)
    auto take_ss_fn = registrar::get<std::function<std::string(const std::string&, const std::string&, int, int)>>("take_screenshot");
    EXPECT_TRUE(take_ss_fn && *take_ss_fn) << "take_screenshot service should be registered in registrar";

    std::filesystem::path snapshot_edited_path = temp_dir / "edited_editor_snapshot.png";
    std::filesystem::path snapshot_blank_path = temp_dir / "blank_editor_snapshot.png";

    // Take screenshot of the edited document
    std::cout << "[Test Debug] editor.empty() = " << (editor.empty() ? "true" : "false") << '\n';
    std::string ss_result = editor.take_snapshot(snapshot_edited_path.string(), 800, 600);
    std::cout << "[Test Debug] snapshot result: " << ss_result << '\n';
    EXPECT_TRUE(ss_result.find("\"success\":true") != std::string::npos) << "take_snapshot should report success for edited file: " << ss_result;
    EXPECT_TRUE(std::filesystem::exists(snapshot_edited_path)) << "Snapshot PNG file should exist on disk";
    EXPECT_GT(std::filesystem::file_size(snapshot_edited_path), 0u) << "Snapshot PNG file size should be > 0 bytes";

    // Create a cleared/blank editor and take a snapshot of it for comparison
    rouen::editor::Editor blank_editor;
    std::cout << "[Test Debug] blank_editor.empty() = " << (blank_editor.empty() ? "true" : "false") << '\n';
    std::string blank_result = blank_editor.take_snapshot(snapshot_blank_path.string(), 800, 600);
    EXPECT_TRUE(blank_result.find("\"success\":true") != std::string::npos);

    // 5. Load and compare the two PNG snapshots
    SDL_Surface* surface_edited = IMG_Load(snapshot_edited_path.string().c_str());
    SDL_Surface* surface_blank = IMG_Load(snapshot_blank_path.string().c_str());

    ASSERT_NE(surface_edited, nullptr) << "Failed to load edited screenshot surface: " << SDL_GetError();
    ASSERT_NE(surface_blank, nullptr) << "Failed to load blank screenshot surface: " << SDL_GetError();

    EXPECT_EQ(surface_edited->w, 800);
    EXPECT_EQ(surface_edited->h, 600);

    // Compute pixel difference between edited document screenshot and blank screenshot
    size_t pixel_differences = count_surface_pixel_differences(surface_edited, surface_blank);
    std::cout << "[Test Info] Pixel differences between blank and edited editor screenshot: " << pixel_differences << '\n';

    EXPECT_GT(pixel_differences, 0u) << "Screenshot of edited document must visually differ from a blank editor screenshot!";

    // Cleanup surfaces and temporary files
    SDL_DestroySurface(surface_edited);
    SDL_DestroySurface(surface_blank);

    std::filesystem::remove(file_path);
    std::filesystem::remove(snapshot_edited_path);
    std::filesystem::remove(snapshot_blank_path);
}
