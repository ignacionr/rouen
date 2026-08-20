#include <rouen_plugin_api.hpp>

#include "hello_card.hpp"

#include <memory>
#include <string_view>

namespace {

    // Every plugin must do this once at load time: each module (this
    // DLL, the host EXE) links its own copy of ImGui and therefore has
    // its own private ImGui global state. SetAllocatorFunctions() is a
    // module-wide setting with no per-frame equivalent, so it belongs
    // here. SetCurrentContext() also needs a per-frame refresh, which
    // rouen::plugin::plugin_card::render() already does automatically -
    // this call just gives the plugin a valid context before its first
    // frame. See docs/PLUGINS.md.
    void sync_imgui_state(rouen::plugin::host_services const& services) {
        ImGui::SetCurrentContext(services.imgui_context);
        ImGui::SetAllocatorFunctions(services.imgui_alloc_func, services.imgui_free_func, services.imgui_alloc_user_data);
    }

} // namespace

ROUEN_PLUGIN_EXPORT bool rouen_plugin_init(rouen::plugin::host_services const* services) {
    if (!services || services->abi_version != rouen::plugin::abi_version) {
        return false;
    }

    sync_imgui_state(*services);

    services->register_card(
        "hello",
        [](std::string_view locator) -> std::unique_ptr<rouen::plugin::plugin_card> {
            return std::make_unique<sample_plugin::hello_card>(locator);
        },
        "Hello Plugin");

    services->log("initialized (schema: hello)");
    return true;
}
