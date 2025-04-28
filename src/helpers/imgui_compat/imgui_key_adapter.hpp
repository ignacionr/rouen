#pragma once

#include "imgui.h"

// This file provides compatibility between ImGuiColorTextEdit and newer ImGui versions
// by implementing adapter functions for the deprecated API

namespace ImGuiCompat
{
    // The GetKeyIndex function was removed in newer ImGui versions
    // This adapter provides backward compatibility for ImGuiColorTextEdit
    inline int GetKeyIndex(ImGuiKey key)
    {
        // In newer ImGui versions, the key values themselves are used directly
        // instead of going through a GetKeyIndex indirection
        return (int)key;
    }
}

// Don't use preprocessor defines as they cause too many issues
// Instead, we'll modify TextEditor.cpp to use ImGuiCompat directly