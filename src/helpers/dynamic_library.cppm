module;

#include <utility>
#include <filesystem>
#include <string>
#include <string_view>

export module rouen.helpers.dynamic_library;

export namespace rouen::helpers::dynamic_library {

    void* load(std::filesystem::path const& path);
    void* get_symbol(void* handle, std::string_view name);
    std::string last_error();
    std::string_view platform_extension();

} // namespace rouen::helpers::dynamic_library
