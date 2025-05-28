#pragma once

// This file is designed to include ImGui headers with warnings suppressed
// Use this file instead of directly including imgui.h to avoid -Wnontrivial-memcall warnings

#ifdef __clang__
#pragma clang diagnostic push
// Only suppress nontrivial-memcall for LLVM Clang, not Apple Clang
#if !defined(__apple_build_version__)
#pragma clang diagnostic ignored "-Wnontrivial-memcall"
#endif
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

// Include ImGui headers
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdlrenderer2.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
