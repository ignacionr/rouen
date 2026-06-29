#pragma once

#include "./imgui_include.hpp"
#include <type_traits>
#include <cstdint>

namespace rouen::helpers {

/**
 * C++23-compatible safe conversion utility for ImTextureID compatibility.
 * Handles various ImTextureID definitions across different ImGui builds:
 * - void* (common in FetchContent/vcpkg builds)  
 * - integral types like uint64_t (common in system packages)
 * 
 * Uses decltype and SFINAE to provide compile-time type-safe conversions.
 */
namespace detail {
    // Helper to detect if ImTextureID is a pointer type
    template<typename T>
    constexpr bool is_texture_id_pointer_v = std::is_pointer_v<std::decay_t<T>>;
    
    // Helper to detect if ImTextureID is an integral type
    template<typename T>
    constexpr bool is_texture_id_integral_v = std::is_integral_v<std::decay_t<T>>;
    
    // Check if types are safely convertible
    template<typename From, typename To>
    constexpr bool is_safe_convertible_v = 
        std::is_convertible_v<From, To> || 
        (std::is_integral_v<From> && std::is_integral_v<To>) ||
        (std::is_pointer_v<From> && std::is_pointer_v<To>);

    // Cast a uintptr_t value to TargetType, choosing reinterpret_cast for pointer
    // targets and static_cast for integral targets.  By parameterizing on TargetType
    // the discarded branch of if constexpr is truly dependent and never instantiated,
    // which avoids the hard "reinterpret_cast between integer types" error that clang
    // emits even for non-taken if constexpr branches when the types are concrete.
    template<typename TargetType>
    [[nodiscard]] constexpr auto int_to_target(uintptr_t val) noexcept -> TargetType {
        if constexpr (std::is_pointer_v<TargetType>) {
            return reinterpret_cast<TargetType>(val);
        } else {
            return static_cast<TargetType>(val);
        }
    }
}

/**
 * Primary template for texture ID conversion
 * Uses decltype to infer the correct conversion strategy at compile time
 */
template<typename T>
[[nodiscard]] constexpr auto texture_id_cast(T&& texture_handle) noexcept -> ImTextureID {
    using DecayedT = std::decay_t<T>;
    using TextureIdType = decltype(ImTextureID{});
    
    // Case 1: Already the correct type - no conversion needed
    if constexpr (std::is_same_v<DecayedT, ImTextureID>) {
        return std::forward<T>(texture_handle);
    }
    // Case 2: Both are pointer types - direct cast
    else if constexpr (detail::is_texture_id_pointer_v<TextureIdType> && std::is_pointer_v<DecayedT>) {
        return static_cast<ImTextureID>(texture_handle);
    }
    // Case 3: Both are integral types - direct cast  
    else if constexpr (detail::is_texture_id_integral_v<TextureIdType> && std::is_integral_v<DecayedT>) {
        return static_cast<ImTextureID>(texture_handle);
    }
    // Case 4: Pointer to integral - use uintptr_t as universal bridge
    else if constexpr (detail::is_texture_id_integral_v<TextureIdType> && std::is_pointer_v<DecayedT>) {
        return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture_handle));
    }
    // Case 5: Integral to pointer - use uintptr_t as universal bridge
    else if constexpr (detail::is_texture_id_pointer_v<TextureIdType> && std::is_integral_v<DecayedT>) {
        return detail::int_to_target<ImTextureID>(static_cast<uintptr_t>(texture_handle));
    }
    // Case 6: Fallback - universal conversion via uintptr_t
    else {
        if constexpr (std::is_pointer_v<DecayedT>) {
            const auto int_value = reinterpret_cast<uintptr_t>(texture_handle);
            return detail::int_to_target<ImTextureID>(int_value);
        } else {
            const auto int_value = static_cast<uintptr_t>(texture_handle);
            return detail::int_to_target<ImTextureID>(int_value);
        }
    }
}

/**
 * Convenience function for OpenGL texture IDs (typically GLuint/unsigned int)
 */
template<typename GLTextureType>
[[nodiscard]] constexpr auto gl_texture_cast(GLTextureType gl_texture) noexcept -> ImTextureID {
    static_assert(std::is_integral_v<GLTextureType>, "GL texture ID must be an integral type");
    return texture_id_cast(gl_texture);
}

/**
 * Convenience function for SDL texture pointers
 */
template<typename SDLTexturePtr>
[[nodiscard]] constexpr auto sdl_texture_cast(SDLTexturePtr* sdl_texture) noexcept -> ImTextureID {
    static_assert(std::is_pointer_v<SDLTexturePtr*>, "SDL texture must be a pointer type");
    return texture_id_cast(sdl_texture);
}

/**
 * Type trait to check if a type can be safely converted to ImTextureID
 */
template<typename T>
constexpr bool is_texture_convertible_v = 
    std::is_integral_v<std::decay_t<T>> || 
    std::is_pointer_v<std::decay_t<T>> ||
    std::is_same_v<std::decay_t<T>, ImTextureID>;

/**
 * Compile-time validation that a type can be used with texture_id_cast
 */
template<typename T>
concept TextureConvertible = is_texture_convertible_v<T>;

} // namespace rouen::helpers
