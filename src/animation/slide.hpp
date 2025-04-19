#pragma once

#include "base.hpp"
#include <chrono>

namespace rouen::animation {
    enum class SlideAxis {
        Horizontal,
        Vertical
    };

    class slide : public base {
    public:
        slide(const ImVec2& size, const renderer_t &frame, const transition_t& transition, 
              float duration_seconds = 1.0f, SlideAxis axis = SlideAxis::Horizontal,
              SDL_Renderer* renderer = nullptr)
            : base(size, frame, transition, renderer),
              duration_ms(static_cast<uint64_t>(duration_seconds * 1000)),
              axis(axis),
              start_time(std::chrono::steady_clock::now()) {}

        bool render() override {
            // Use the renderer from the base class
            render(this->sdl_renderer);
            // return false when the timer is done
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
            return elapsed < duration_ms;
        }

        void render(SDL_Renderer* sdl_renderer) {
            // Calculate progress (0.0f to 1.0f)
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
            float progress = std::min(1.0f, static_cast<float>(elapsed) / duration_ms);
            
            // Apply easing function for smoother animation (ease in-out)
            float smoothed_progress = smoothstep(progress);
            
            // Set up source and destination rectangles based on the axis and progress
            SDL_Rect src_rect = {0, 0, static_cast<int>(size.x), static_cast<int>(size.y)};
            SDL_Rect dst_rect = {0, 0, static_cast<int>(size.x), static_cast<int>(size.y)};
            
            // Render the "from" texture with position based on progress
            if (progress < 1.0f) {
                if (axis == SlideAxis::Horizontal) {
                    dst_rect.x = -static_cast<int>(smoothed_progress * size.x);
                } else {
                    dst_rect.y = -static_cast<int>(smoothed_progress * size.y);
                }
                SDL_SetTextureAlphaMod(from_texture, 255);
                SDL_RenderCopy(sdl_renderer, from_texture, &src_rect, &dst_rect);
            }
            
            // Render the "to" texture with position based on progress
            if (axis == SlideAxis::Horizontal) {
                dst_rect.x = static_cast<int>(size.x * (1.0f - smoothed_progress));
            } else {
                dst_rect.y = static_cast<int>(size.y * (1.0f - smoothed_progress));
            }
            SDL_SetTextureAlphaMod(to_texture, 255);
            SDL_RenderCopy(sdl_renderer, to_texture, &src_rect, &dst_rect);
        }
        
    private:
        uint64_t duration_ms;
        SlideAxis axis;
        std::chrono::time_point<std::chrono::steady_clock> start_time;
        
        // Smoothstep function for easing (cubic Hermite interpolation)
        float smoothstep(float x) const {
            // Clamp between 0 and 1
            x = std::max(0.0f, std::min(1.0f, x));
            // Apply smoothstep: 3x^2 - 2x^3
            return x * x * (3.0f - 2.0f * x);
        }
    };
}