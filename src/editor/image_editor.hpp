#pragma once

#include <string>
#include "../helpers/imgui_include.hpp"
#include <SDL.h>
#include <SDL_image.h>

#include "editor_interface.hpp"
#include "../helpers/texture_helper.hpp"
#include "../registrar.hpp"

namespace rouen {
namespace editor {

class ImageEditor : public EditorInterface {
public:
    ImageEditor() 
        : 
          should_focus_{false}
        , image_texture_{nullptr}
        , image_width_{0}
        , image_height_{0}
    {
        // Initialize colors
        error_color = {1.0f, 0.0f, 0.0f, 1.0f};   // Red error text
        
        try {
            // Get the SDL renderer from the registrar
            renderer_ = *registrar::get<SDL_Renderer*>("main_renderer");
        } catch (const std::exception& e) {
            // No renderer available
            renderer_ = nullptr;
        }
    }
    
    virtual ~ImageEditor() {
        if (image_texture_) {
            SDL_DestroyTexture(image_texture_);
            image_texture_ = nullptr;
        }
    }

    bool empty() const override {
        return source_file_.empty();
    }

    void clear() override {
        source_file_.clear();
        error_.clear();
        
        if (image_texture_) {
            SDL_DestroyTexture(image_texture_);
            image_texture_ = nullptr;
            image_width_ = 0;
            image_height_ = 0;
        }
    }

    void select(const std::string& uri) override {
        source_file_ = uri;
        
        // Set flag to focus the window on next render
        should_focus_ = true;
        
        // Clear previous resources
        if (image_texture_) {
            SDL_DestroyTexture(image_texture_);
            image_texture_ = nullptr;
            image_width_ = 0;
            image_height_ = 0;
        }
        
        error_.clear();
        
        if (renderer_) {
            // Load image as texture
            image_texture_ = TextureHelper::loadTextureFromFile(
                renderer_,
                uri.c_str(),
                image_width_,
                image_height_
            );
            
            if (!image_texture_) {
                error_ = "Failed to load image: " + std::string(SDL_GetError());
            }
        } else {
            error_ = "Cannot load image: SDL renderer not available";
        }
    }

    bool saveFile() override {
        // Image editor doesn't support saving directly
        return false;
    }

    void render() override {
        if (!image_texture_ && error_.empty()) {
            return;
        }

        if (!error_.empty()) {
            ImGui::TextColored(error_color, "Error: %s", error_.c_str());
        }
        else if (image_texture_) {
            // Display the image
            ImGui::Separator();
            
            // Calculate the display size while maintaining aspect ratio
            ImVec2 content_size = ImGui::GetContentRegionAvail();
            float aspect_ratio = static_cast<float>(image_width_) / static_cast<float>(image_height_);
            
            ImVec2 display_size;
            if (content_size.x / aspect_ratio <= content_size.y) {
                // Width constrained
                display_size = ImVec2(content_size.x, content_size.x / aspect_ratio);
            } else {
                // Height constrained
                display_size = ImVec2(content_size.y * aspect_ratio, content_size.y);
            }
            
            // Center the image in the available space
            ImVec2 pos = ImGui::GetCursorPos();
            pos.x += (content_size.x - display_size.x) * 0.5f;
            ImGui::SetCursorPos(pos);
            
            // Draw the image
            ImGui::Image((ImTextureID)(intptr_t)image_texture_, display_size);
        }
    }

    bool shouldFocus() const {
        return should_focus_;
    }

    void resetFocus() {
        should_focus_ = false;
    }

private:
    std::string source_file_;
    std::string error_;
    bool should_focus_ = false;  // Flag to track when the window should grab focus
    
    // Image handling
    SDL_Renderer* renderer_ {nullptr};
    SDL_Texture* image_texture_ {nullptr};
    int image_width_ = 0;
    int image_height_ = 0;

    // Color variable
    ImVec4 error_color;
};

} // namespace editor
} // namespace rouen
