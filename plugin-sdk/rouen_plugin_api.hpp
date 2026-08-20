#pragma once

// Rouen Plugin SDK
//
// This header is the entire contract between Rouen and a plugin. It has
// no dependency on any of Rouen's internal headers (card.hpp, registrar.hpp,
// factory.hpp, ...) on purpose: those headers carry inline code with
// per-translation-unit static state, and including them from a plugin
// would silently give the plugin its own private copy of that state
// instead of sharing the host's. ImGui is the one exception, because a
// plugin card's whole job is to draw with it - see host_services below
// for how its state is synchronized across the DLL boundary.
//
// A plugin built against this header MUST use the same compiler, C++
// standard library, ImGui version, and CRT linkage (dynamic /MD on
// Windows) as the Rouen build it targets. See docs/PLUGINS.md for the
// full set of requirements and a worked example.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <imgui.h>

#if defined(_WIN32)
    #define ROUEN_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
    #define ROUEN_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Exported symbol name a plugin must define with ROUEN_PLUGIN_EXPORT.
#define ROUEN_PLUGIN_ENTRY_POINT_NAME "rouen_plugin_init"

namespace rouen::plugin {

    // Bump when host_services or plugin_card change in a way that is not
    // source/binary compatible with older plugins.
    inline constexpr std::uint32_t abi_version{1};

    // Base class for a plugin-provided card. The host wraps every
    // instance in an adapter that behaves like any built-in card (drag,
    // resize, focus, theming, window decorations); the plugin only
    // implements its own content.
    class plugin_card {
    public:
        virtual ~plugin_card() = default;

        // Called once per frame while the card's window is visible, with
        // whichever ImGuiContext the host is currently rendering into.
        // The default implementation re-synchronizes this DLL's own
        // ImGui module state to that context before calling draw() -
        // required because each module (this plugin, the host EXE) that
        // links ImGui has its own private copy of ImGui's globals, and
        // the host is not always rendering into the same context: it
        // briefly switches to a second, offscreen one to produce card
        // snapshots (see the REST API's /api/cards/snapshot). A plugin
        // must not override this method - override draw() instead.
        virtual void render(ImGuiContext* active_context) {
            ImGui::SetCurrentContext(active_context);
            draw();
        }

        // Draws the card's ImGui content. Called by render() (above)
        // with the correct context already current - implement this,
        // not render().
        virtual void draw() = 0;

        // Window title shown in the card's title bar.
        [[nodiscard]] virtual std::string title() const = 0;

        // Canonical URI for this card instance (e.g. "hello:World"), used
        // for workspace-layout persistence and the REST API.
        [[nodiscard]] virtual std::string uri() const = 0;

        // Optional: react to a new locator without recreating the card.
        virtual void handle_uri(std::string_view /*locator*/) {}

        // Optional: called once when the card is closed.
        virtual void on_close() {}
    };

    // Given the locator portion of a card URI (the text after the first
    // ':', or empty if none), produces a new card instance. Returning
    // nullptr fails the creation request.
    using card_factory_fn = std::function<std::unique_ptr<plugin_card>(std::string_view locator)>;

    // Services the host hands a plugin at load time. A plugin must
    // synchronize its own ImGui module state from the imgui_* fields
    // (see docs/PLUGINS.md) before making any ImGui:: call.
    struct host_services {
        std::uint32_t abi_version{0};

        ImGuiContext* imgui_context{nullptr};
        ImGuiMemAllocFunc imgui_alloc_func{nullptr};
        ImGuiMemFreeFunc imgui_free_func{nullptr};
        void* imgui_alloc_user_data{nullptr};

        // Registers a new card schema (the URI prefix before ':'). When
        // display_name is non-empty, the card is also added to the
        // deck's "Plugins" launcher menu under that name; leave it empty
        // for schemas the plugin only wants reachable by URI (e.g.
        // sub-views of another plugin card).
        std::function<void(std::string const& schema, card_factory_fn factory, std::string const& display_name)> register_card;

        // Logs a line prefixed with the plugin's file name in the host's
        // console/log output.
        std::function<void(std::string const& message)> log;
    };

    // Signature a plugin exports under ROUEN_PLUGIN_ENTRY_POINT_NAME.
    // Return false to abort loading (e.g. on an abi_version mismatch);
    // the host will keep the library mapped but treat it as failed.
    using plugin_init_fn = bool (*)(host_services const* services);

} // namespace rouen::plugin
