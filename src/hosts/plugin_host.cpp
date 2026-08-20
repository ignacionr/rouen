#include "plugin_host.hpp"

// 1. Standard includes in alphabetic order
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

// 2. Libraries used in the project, in alphabetic order
#include "../helpers/imgui_include.hpp"

// 3. All other includes
#include "../cards/interface/factory.hpp"
#include "../cards/interface/plugin_card_adapter.hpp"
#include "../helpers/dynamic_library.hpp"
#include "../helpers/platform_utils.hpp"
#include "rouen_plugin_api.hpp"

namespace rouen::hosts {

    plugin_host& plugin_host::instance() {
        static plugin_host inst;
        return inst;
    }

    void plugin_host::load_all_plugins() {
        // Bundled alongside the executable, e.g. shipped by an installer.
        load_from_directory(rouen::platform::get_executable_directory() / "plugins");

        // User-writable location; created eagerly so plugin authors and
        // users have an obvious place to drop a library.
        auto user_plugins_dir = rouen::platform::get_user_data_path("", true) / "plugins";
        std::error_code ec;
        std::filesystem::create_directories(user_plugins_dir, ec);
        load_from_directory(user_plugins_dir);
    }

    void plugin_host::load_from_directory(std::filesystem::path const& dir) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
            return;
        }

        auto const extension = rouen::helpers::dynamic_library::platform_extension();
        std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec);
        std::filesystem::directory_iterator const end;
        for (; it != end && !ec; it.increment(ec)) {
            std::error_code file_ec;
            if (it->is_regular_file(file_ec) && it->path().extension() == extension) {
                load_plugin(it->path());
            }
        }
    }

    void plugin_host::load_plugin(std::filesystem::path const& library_path) {
        std::string const plugin_name = library_path.filename().string();

        void* handle = rouen::helpers::dynamic_library::load(library_path);
        if (!handle) {
            std::cerr << "[plugin] failed to load " << plugin_name << ": "
                      << rouen::helpers::dynamic_library::last_error() << '\n';
            return;
        }

        void* raw_init = rouen::helpers::dynamic_library::get_symbol(handle, ROUEN_PLUGIN_ENTRY_POINT_NAME);
        if (!raw_init) {
            std::cerr << "[plugin] " << plugin_name << " does not export " << ROUEN_PLUGIN_ENTRY_POINT_NAME << '\n';
            return;
        }
        auto const init_fn = reinterpret_cast<rouen::plugin::plugin_init_fn>(raw_init); // NOLINT

        rouen::plugin::host_services services{};
        services.abi_version = rouen::plugin::abi_version;
        services.imgui_context = ImGui::GetCurrentContext();
        ImGui::GetAllocatorFunctions(&services.imgui_alloc_func, &services.imgui_free_func, &services.imgui_alloc_user_data);

        services.register_card = [](std::string const& schema, rouen::plugin::card_factory_fn factory,
                                     std::string const& display_name) {
            rouen::cards::factory::register_card(
                schema,
                [factory](std::string_view locator, SDL_Renderer*) -> card::ptr {
                    auto impl = factory(locator);
                    if (!impl) {
                        return nullptr;
                    }
                    return std::make_shared<rouen::cards::plugin_card_adapter>(std::move(impl));
                },
                display_name);
        };

        services.log = [plugin_name](std::string const& message) {
            std::cout << "[plugin:" << plugin_name << "] " << message << '\n';
        };

        bool init_ok = false;
        try {
            init_ok = init_fn(&services);
        } catch (std::exception const& e) {
            std::cerr << "[plugin] " << plugin_name << " threw during init: " << e.what() << '\n';
            return;
        } catch (...) {
            std::cerr << "[plugin] " << plugin_name << " threw an unknown exception during init\n";
            return;
        }

        if (init_ok) {
            std::cout << "[plugin] loaded " << plugin_name << '\n';
            loaded_plugins_.push_back(plugin_name);
        } else {
            std::cerr << "[plugin] " << plugin_name << " reported an initialization failure\n";
        }
    }

} // namespace rouen::hosts
