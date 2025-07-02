#pragma once

#include "./imgui_include.hpp"

namespace rouen::helpers {

/**
 * Safe conversion utility for ImTextureID compatibility
 * With FetchContent ImGui, ImTextureID is void*, so this is a simple pass-through
 */
inline ImTextureID texture_id_cast(void* ptr) {
    return ptr;
}

} // namespace rouen::helpers
