#include <rouen_plugin_api.hpp>

#include "jira_card.hpp"
#include "jira_projects.hpp"
#include "jira_search.hpp"

#include <memory>
#include <string_view>

namespace {

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
        "jira",
        [](std::string_view locator) -> std::unique_ptr<rouen::plugin::plugin_card> {
            return std::make_unique<rouen::cards::jira_card>(locator);
        },
        "Jira");

    services->register_card(
        "jira-projects",
        [](std::string_view) -> std::unique_ptr<rouen::plugin::plugin_card> {
            return std::make_unique<rouen::cards::jira_projects_card>();
        },
        "Jira Projects");

    services->register_card(
        "jira-search",
        [](std::string_view locator) -> std::unique_ptr<rouen::plugin::plugin_card> {
            auto card = std::make_unique<rouen::cards::jira_search_card>();
            if (!locator.empty()) {
                card->handle_uri(locator);
            }
            return card;
        },
        "Jira Search");

    services->log("initialized (schemas: jira, jira-projects, jira-search)");
    return true;
}
