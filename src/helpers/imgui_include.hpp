#pragma once

// This file is designed to include ImGui headers with warnings suppressed
// Use this file instead of directly including imgui.h to avoid -Wnontrivial-memcall warnings

#ifdef __clang__
#pragma clang diagnostic push
// Note: Removed -Wnontrivial-memcall suppression due to version compatibility issues
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

// Include ImGui headers
#include <imgui.h>
#include <imgui_internal.h>

// Backend headers - try both system and local paths
#if __has_include(<backends/imgui_impl_sdl3.h>)
// FetchContent ImGui with built-in backends
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#else
// System ImGui with local backends
#include "../../external/imgui_backends/imgui_impl_sdl3.h"
#include "../../external/imgui_backends/imgui_impl_sdlgpu3.h"
#endif

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
