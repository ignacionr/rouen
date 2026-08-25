#include <rouen_plugin_api.hpp>

#include "trello_card.hpp"

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
        "trello",
        [](std::string_view locator) -> std::unique_ptr<rouen::plugin::plugin_card> {
            auto card = std::make_unique<rouen::cards::trello_card>();
            if (!locator.empty()) {
                card->handle_uri(locator);
            }
            return card;
        },
        "Trello");

    services->register_card(
        "trello-board",
        [](std::string_view locator) -> std::unique_ptr<rouen::plugin::plugin_card> {
            auto card = std::make_unique<rouen::cards::trello_card>(std::string(locator), rouen::cards::trello_card::card_context::board_specific);
            return card;
        },
        "Trello Board");

    services->register_card(
        "trello-card",
        [](std::string_view locator) -> std::unique_ptr<rouen::plugin::plugin_card> {
            auto card = std::make_unique<rouen::cards::trello_card>(std::string(locator), rouen::cards::trello_card::card_context::card_specific);
            return card;
        },
        "Trello Card");

    services->log("initialized (schemas: trello, trello-board, trello-card)");
    return true;
}
